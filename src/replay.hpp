#ifndef _replay_hpp
#define _replay_hpp

#include <Geode/Geode.hpp>
#include <gdr/gdr.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

using namespace geode::prelude;
#define ZBF_VERSION 3.1f

// ============================================================================
// zInput — a single frame-level input event stored in a replay
// ============================================================================
struct zInput : gdr::Input {
    zInput() = default;

    zInput(int frame, int button, bool player2, bool down)
        : Input(frame, button, player2, down) {}
};

// ============================================================================
// zReplaySample — position/progress anchor used for playback seek
// ============================================================================
struct zReplaySample {
    int replayFrame = 0;
    unsigned int gameProgress = 0;
    float percent = 0.f;
    float player1X = 0.f;
    float player1Y = 0.f;
    float player2X = 0.f;
    float player2Y = 0.f;
    bool dual = false;
    bool twoPlayer = false;

    nlohmann::json toJson() const {
        nlohmann::json json;
        json["frame"] = replayFrame;
        json["gameProgress"] = gameProgress;
        json["percent"] = percent;
        json["player1X"] = player1X;
        json["player1Y"] = player1Y;
        json["player2X"] = player2X;
        json["player2Y"] = player2Y;
        json["dual"] = dual;
        json["twoPlayer"] = twoPlayer;
        return json;
    }

    static std::optional<zReplaySample> fromJson(nlohmann::json const& json) {
        if (!json.is_object() || !json.contains("frame")) {
            return std::nullopt;
        }

        zReplaySample sample;
        sample.replayFrame = json.value("frame", 0);
        sample.gameProgress = json.value("gameProgress", 0u);
        sample.percent = json.value("percent", 0.f);
        sample.player1X = json.value("player1X", 0.f);
        sample.player1Y = json.value("player1Y", 0.f);
        sample.player2X = json.value("player2X", 0.f);
        sample.player2Y = json.value("player2Y", 0.f);
        sample.dual = json.value("dual", false);
        sample.twoPlayer = json.value("twoPlayer", false);
        return sample;
    }
};

// ============================================================================
// zReplay — a complete replay file backed by the GDR binary format
// ============================================================================
struct zReplay : gdr::Replay<zReplay, zInput> {
    static constexpr int METADATA_SCHEMA = 1;

    std::string name;
    std::vector<zReplaySample> positionSamples;
    bool recordedFromStartPos = false;
    unsigned int recordingStartGameProgress = 0;
    float recordingStartPercent = 0.f;
    float recordingStartPlayer1X = 0.f;
    float recordingStartPlayer1Y = 0.f;
    std::string recordingStartSource = "level-start";

    zReplay() : Replay("zBot", MOD_VERSION) {}

    void parseExtension(nlohmann::json::object_t obj) override {
        auto replayJson = nlohmann::json(obj);
        if (!replayJson.contains("zbotMeta") || !replayJson["zbotMeta"].is_object()) {
            return;
        }

        auto const& meta = replayJson["zbotMeta"];
        recordedFromStartPos = meta.value("recordedFromStartPos", false);
        recordingStartGameProgress = meta.value("recordingStartGameProgress", 0u);
        recordingStartPercent = meta.value("recordingStartPercent", 0.f);
        recordingStartPlayer1X = meta.value("recordingStartPlayer1X", 0.f);
        recordingStartPlayer1Y = meta.value("recordingStartPlayer1Y", 0.f);
        recordingStartSource = meta.value("recordingStartSource", std::string("level-start"));

        positionSamples.clear();
        if (!meta.contains("positionSamples") || !meta["positionSamples"].is_array()) {
            return;
        }

        for (auto const& sampleJson : meta["positionSamples"]) {
            if (auto sample = zReplaySample::fromJson(sampleJson)) {
                positionSamples.push_back(*sample);
            }
        }

        std::sort(positionSamples.begin(), positionSamples.end(), [](zReplaySample const& left, zReplaySample const& right) {
            return left.replayFrame < right.replayFrame;
        });
    }

    nlohmann::json::object_t saveExtension() const override {
        nlohmann::json replayJson;
        nlohmann::json meta;
        meta["schema"] = METADATA_SCHEMA;
        meta["recordedFromStartPos"] = recordedFromStartPos;
        meta["recordingStartGameProgress"] = recordingStartGameProgress;
        meta["recordingStartPercent"] = recordingStartPercent;
        meta["recordingStartPlayer1X"] = recordingStartPlayer1X;
        meta["recordingStartPlayer1Y"] = recordingStartPlayer1Y;
        meta["recordingStartSource"] = recordingStartSource;

        for (auto const& sample : positionSamples) {
            meta["positionSamples"].push_back(sample.toJson());
        }

        replayJson["zbotMeta"] = meta;
        return replayJson.get<nlohmann::json::object_t>();
    }

    /// Serialise and write this replay to disk under the mod's save directory.
    void save() {
        author = GJAccountManager::get()->m_username;
        duration = inputs.size() > 0
            ? static_cast<float>(inputs.back().frame) / framerate
            : 0.f;

        auto dir = Mod::get()->getSaveDir() / "replays";
        if (!std::filesystem::exists(dir))
            std::filesystem::create_directories(dir);

        std::ofstream f(dir / (name + ".gdr"), std::ios::binary);
        auto data = exportData(false);
        f.write(reinterpret_cast<const char*>(data.data()), data.size());
        f.close();

        log::info(
            "Saved replay '{}' | inputs={} samples={} startSource={} startPos={}",
            name,
            inputs.size(),
            positionSamples.size(),
            recordingStartSource,
            recordedFromStartPos
        );
    }

    /// Attempt to load a replay from the mod's replay directory.
    static zReplay* fromFile(const std::string& fileName) {
        auto dir = Mod::get()->getSaveDir() / "replays";
        if (!std::filesystem::exists(dir))
            std::filesystem::create_directories(dir);

        std::ifstream f(dir / (fileName + ".gdr"), std::ios::binary);
        if (!f.is_open()) {
            f = std::ifstream(dir / fileName, std::ios::binary);
            if (!f.is_open()) return nullptr;
        }

        f.seekg(0, std::ios::end);
        auto size = f.tellg();
        f.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(static_cast<size_t>(size));
        f.read(reinterpret_cast<char*>(data.data()), size);
        f.close();

        zReplay* ret = new zReplay();
        *ret = zReplay::importData(data);
        ret->name = fileName;

        log::info(
            "Loaded replay '{}' | inputs={} samples={} startSource={} startPos={}",
            ret->name,
            ret->inputs.size(),
            ret->positionSamples.size(),
            ret->recordingStartSource,
            ret->recordedFromStartPos
        );

        return ret;
    }

    /// Remove all inputs at or after the given frame.
    void purgeAfter(int frame) {
        inputs.erase(
            std::remove_if(inputs.begin(), inputs.end(),
                [frame](zInput& input) { return input.frame >= frame; }),
            inputs.end());

        positionSamples.erase(
            std::remove_if(positionSamples.begin(), positionSamples.end(),
                [frame](zReplaySample const& sample) { return sample.replayFrame > frame; }),
            positionSamples.end());
    }

    /// Append a new input event.
    void addInput(int frame, int button, bool player2, bool down) {
        log::info("Adding input: frame={}, button={}, player2={}, down={}",
                  frame, button, player2, down);
        inputs.emplace_back(frame, button, player2, down);
    }

    /// Insert or replace a replay position sample.
    void addPositionSample(zReplaySample const& sample, bool replaceExisting = true) {
        auto it = std::lower_bound(positionSamples.begin(), positionSamples.end(), sample.replayFrame,
            [](zReplaySample const& existing, int frame) {
                return existing.replayFrame < frame;
            }
        );

        if (it != positionSamples.end() && it->replayFrame == sample.replayFrame) {
            if (replaceExisting) {
                *it = sample;
            }
            return;
        }

        positionSamples.insert(it, sample);
    }
};

#endif
