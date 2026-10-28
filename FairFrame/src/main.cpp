#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {
    struct FrameEvent {
        int attempt = 1;
        int frame = 0;
        int player = 1;
        int deltaFrames = 0;
        int holdFrames = -1;
        bool pressed = true;
    };

    struct FairFrameState {
        PlayLayer* activeLayer = nullptr;
        CCLayerColor* hudPanel = nullptr;
        CCLabelBMFont* hudTitle = nullptr;
        CCLabelBMFont* hudBody = nullptr;
        int frame = 0;
        int attempt = 1;
        int previousEventFrame = -1;
        int previousEventAttempt = 1;
        bool recording = false;
        bool playing = false;
        bool showHud = true;
        bool playbackFinished = false;
        size_t replayIndex = 0;
        std::string lastMessage = "READY";
        std::vector<FrameEvent> events;
        std::map<int, int> pressStartedAt;
        std::map<int, bool> replayHeld;
    };

    FairFrameState g_state;

    int clickCount() {
        return static_cast<int>(std::count_if(
            g_state.events.begin(), g_state.events.end(),
            [](FrameEvent const& event) { return event.pressed; }
        ));
    }

    std::string currentLevelName(PlayLayer* layer) {
        if (!layer || !layer->m_level) return "Unknown-Level";
        std::string name = layer->m_level->m_levelName;
        return name.empty() ? "Unknown-Level" : name;
    }

    std::string safeFilename(std::string value) {
        for (char& c : value) {
            const auto uc = static_cast<unsigned char>(c);
            if (!(std::isalnum(uc) || c == '-' || c == '_')) c = '_';
        }
        if (value.empty()) value = "Unknown-Level";
        return value;
    }

    std::filesystem::path takePath(PlayLayer* layer) {
        return Mod::get()->getSaveDir() /
            ("FairFrame-" + safeFilename(currentLevelName(layer)) + "-latest.csv");
    }

    int toInt(std::string const& text, int fallback = 0) {
        try {
            size_t consumed = 0;
            int value = std::stoi(text, &consumed);
            return consumed ? value : fallback;
        } catch (...) {
            return fallback;
        }
    }

    std::vector<std::string> splitCsvLine(std::string const& line) {
        std::vector<std::string> columns;
        std::stringstream stream(line);
        std::string cell;
        while (std::getline(stream, cell, ',')) columns.push_back(cell);
        if (!line.empty() && line.back() == ',') columns.emplace_back();
        return columns;
    }

    std::string modeText() {
        if (g_state.playing) return "REPLAYING";
        if (g_state.recording) return "RECORDING";
        if (!g_state.events.empty()) return "TAKE READY";
        return "IDLE";
    }

    std::string statusText() {
        std::ostringstream out;
        out << modeText()
            << "  |  FRAME " << g_state.frame
            << "  |  CLICKS " << clickCount()
            << "  |  EVENTS " << g_state.events.size();
        return out.str();
    }

    std::string lastEventText() {
        if (g_state.events.empty()) return "LAST INPUT: --";
        auto const& event = g_state.events.back();
        std::ostringstream out;
        out << "LAST P" << event.player << " "
            << (event.pressed ? "PRESS" : "RELEASE")
            << " @F" << event.frame << "  DELTA " << event.deltaFrames;
        if (!event.pressed && event.holdFrames >= 0) out << "  HOLD " << event.holdFrames;
        return out.str();
    }

    std::string recentEventsText() {
        std::ostringstream out;
        out << "RECENT INPUTS";
        if (g_state.events.empty()) {
            out << "\nNo take saved yet";
            return out.str();
        }

        size_t first = g_state.events.size() > 3 ? g_state.events.size() - 3 : 0;
        for (size_t i = first; i < g_state.events.size(); ++i) {
            auto const& event = g_state.events[i];
            out << "\nP" << event.player << "  F" << event.frame << "  "
                << (event.pressed ? "PRESS" : "RELEASE")
                << "  +" << event.deltaFrames;
            if (!event.pressed && event.holdFrames >= 0) out << "  HOLD " << event.holdFrames;
        }
        return out.str();
    }

    void removeHud() {
        if (g_state.hudPanel) {
            g_state.hudPanel->removeFromParentAndCleanup(true);
        }
        g_state.hudPanel = nullptr;
        g_state.hudTitle = nullptr;
        g_state.hudBody = nullptr;
    }

    void createHud(PlayLayer* layer) {
        if (!layer || !g_state.showHud || g_state.hudPanel) return;

        auto size = CCDirector::sharedDirector()->getWinSize();
        auto panel = CCLayerColor::create(ccc4(9, 17, 33, 226), 286.f, 96.f);
        if (!panel) return;
        panel->setPosition({14.f, size.height - 110.f});
        panel->setID("fairframe-live-hud"_spr);

        auto accent = CCLayerColor::create(ccc4(68, 224, 255, 255), 3.f, 96.f);
        panel->addChild(accent, 2);

        auto title = CCLabelBMFont::create("FAIRFRAME  /  FRAME LAB", "bigFont.fnt");
        if (!title) return;
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({13.f, 77.f});
        title->setScale(0.42f);
        title->setColor(ccc3(90, 228, 255));
        panel->addChild(title);

        auto body = CCLabelBMFont::create("IDLE", "chatFont.fnt");
        if (!body) return;
        body->setAnchorPoint({0.f, 1.f});
        body->setPosition({13.f, 63.f});
        body->setScale(0.47f);
        body->setColor(ccc3(231, 240, 255));
        panel->addChild(body);

        layer->addChild(panel, 10000);
        g_state.hudPanel = panel;
        g_state.hudTitle = title;
        g_state.hudBody = body;
    }

    void updateHud(PlayLayer* layer) {
        if (!g_state.showHud) {
            removeHud();
            return;
        }
        if (!g_state.hudPanel || g_state.hudPanel->getParent() != layer) {
            removeHud();
            createHud(layer);
        }

        if (g_state.hudBody) {
            std::ostringstream text;
            text << (g_state.recording ? "● RECORDING" : g_state.playing ? "▶ REPLAYING" : "○ " + modeText())
                 << "\nFRAME " << g_state.frame
                 << "     CLICKS " << clickCount()
                 << "     EVENTS " << g_state.events.size()
                 << "\n" << lastEventText()
                 << "\n" << g_state.lastMessage;
            g_state.hudBody->setString(text.str().c_str());
        }
    }

    bool exportTake(PlayLayer* layer, bool quiet = false) {
        if (g_state.events.empty()) {
            if (!quiet) {
                FLAlertLayer::create("FairFrame", "No inputs are recorded yet. Start recording, play a take, then export.", "OK")->show();
            }
            return false;
        }

        std::error_code ec;
        auto folder = Mod::get()->getSaveDir();
        std::filesystem::create_directories(folder, ec);
        auto path = takePath(layer);
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file) {
            log::error("FairFrame could not write {}", path.string());
            if (!quiet) FLAlertLayer::create("FairFrame", "Could not save the take. Check the Geode save folder permissions.", "OK")->show();
            return false;
        }

        file << "# FairFrame input take\n";
        file << "# Level: " << currentLevelName(layer) << "\n";
        file << "# Replay restarts from the start of the level; checkpoints are not reproduced.\n";
        file << "attempt,frame,player,action,delta_frames,hold_frames\n";
        for (auto const& event : g_state.events) {
            file << event.attempt << ','
                 << event.frame << ','
                 << event.player << ','
                 << (event.pressed ? "PRESS" : "RELEASE") << ','
                 << event.deltaFrames << ',';
            if (event.holdFrames >= 0) file << event.holdFrames;
            file << '\n';
        }
        file.close();

        log::info("FairFrame saved {} events to {}", g_state.events.size(), path.string());
        if (!quiet) {
            FLAlertLayer::create(
                "FairFrame — Take Saved",
                ("Saved " + std::to_string(g_state.events.size()) + " events for " + currentLevelName(layer) + ".\n\nCSV:\n" + path.string()).c_str(),
                "Great"
            )->show();
        }
        return true;
    }

    bool loadTake(PlayLayer* layer, bool announce = false) {
        if (!layer) return false;
        const auto path = takePath(layer);
        std::ifstream file(path);
        if (!file) return false;

        bool hasPlayerColumn = false;
        std::vector<FrameEvent> loaded;
        std::string line;
        int latestAttempt = 1;

        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line.rfind("attempt,", 0) == 0) {
                hasPlayerColumn = line.find(",player,") != std::string::npos;
                continue;
            }

            auto columns = splitCsvLine(line);
            const size_t minColumns = hasPlayerColumn ? 5 : 3;
            if (columns.size() < minColumns) continue;

            FrameEvent event;
            event.attempt = toInt(columns[0], 1);
            event.frame = std::max(0, toInt(columns[1], 0));
            size_t actionIndex = 2;
            size_t deltaIndex = 3;
            size_t holdIndex = 4;
            if (hasPlayerColumn) {
                if (columns.size() < 5) continue;
                event.player = std::clamp(toInt(columns[2], 1), 1, 2);
                actionIndex = 3;
                deltaIndex = 4;
                holdIndex = 5;
            } else {
                event.player = 1;
            }

            if (actionIndex >= columns.size()) continue;
            event.pressed = columns[actionIndex] == "PRESS";
            event.deltaFrames = deltaIndex < columns.size() ? std::max(0, toInt(columns[deltaIndex], 0)) : 0;
            event.holdFrames = holdIndex < columns.size() ? toInt(columns[holdIndex], -1) : -1;
            latestAttempt = std::max(latestAttempt, event.attempt);
            loaded.push_back(event);
        }

        g_state.events.clear();
        for (auto event : loaded) {
            if (event.attempt == latestAttempt) {
                event.attempt = 1;
                g_state.events.push_back(event);
            }
        }

        g_state.previousEventFrame = g_state.events.empty() ? -1 : g_state.events.back().frame;
        g_state.previousEventAttempt = 1;
        if (!g_state.events.empty()) {
            g_state.lastMessage = "LAST TAKE LOADED";
            if (announce) {
                FLAlertLayer::create(
                    "FairFrame — Take Ready",
                    ("Loaded " + std::to_string(g_state.events.size()) + " recorded events.").c_str(),
                    "OK"
                )->show();
            }
        }
        return !g_state.events.empty();
    }

    PlayerObject* playerFor(PlayLayer* layer, int playerNumber) {
        if (!layer) return nullptr;
        return playerNumber == 2 ? layer->m_player2 : layer->m_player1;
    }

    void releaseReplayButtons(PlayLayer* layer) {
        for (auto const& [playerNumber, isDown] : g_state.replayHeld) {
            if (!isDown) continue;
            if (auto player = playerFor(layer, playerNumber)) {
                player->releaseButton(PlayerButton::Jump);
            }
        }
        g_state.replayHeld.clear();
    }

    void stopReplay(PlayLayer* layer, std::string message) {
        g_state.playing = false;
        releaseReplayButtons(layer);
        g_state.lastMessage = std::move(message);
    }

    void beginRecording() {
        g_state.events.clear();
        g_state.pressStartedAt.clear();
        g_state.replayIndex = 0;
        g_state.playing = false;
        g_state.playbackFinished = false;
        g_state.frame = 0;
        g_state.attempt = 1;
        g_state.previousEventFrame = -1;
        g_state.previousEventAttempt = 1;
        g_state.recording = true;
        g_state.lastMessage = "RECORDING NEW TAKE";
    }

    void recordInput(bool pressed, PlayerObject* player) {
        if (!g_state.recording || g_state.playing || !player || PlayLayer::get() != g_state.activeLayer) return;

        const int playerNumber = player->isPlayer2() ? 2 : 1;
        int holdFrames = -1;
        if (pressed) {
            g_state.pressStartedAt[playerNumber] = g_state.frame;
        } else {
            auto it = g_state.pressStartedAt.find(playerNumber);
            if (it != g_state.pressStartedAt.end()) {
                holdFrames = std::max(0, g_state.frame - it->second);
                g_state.pressStartedAt.erase(it);
            }
        }

        int delta = 0;
        if (g_state.previousEventAttempt == g_state.attempt && g_state.previousEventFrame >= 0) {
            delta = std::max(0, g_state.frame - g_state.previousEventFrame);
        }

        g_state.events.push_back(FrameEvent{
            g_state.attempt,
            g_state.frame,
            playerNumber,
            delta,
            holdFrames,
            pressed
        });
        g_state.previousEventFrame = g_state.frame;
        g_state.previousEventAttempt = g_state.attempt;

        if (g_state.events.size() > 50000) {
            g_state.events.erase(g_state.events.begin(), g_state.events.begin() + 5000);
        }
    }

    void startLevel(PlayLayer* layer) {
        removeHud();
        g_state.activeLayer = layer;
        g_state.frame = 0;
        g_state.attempt = 1;
        g_state.previousEventFrame = -1;
        g_state.previousEventAttempt = 1;
        g_state.pressStartedAt.clear();
        g_state.replayHeld.clear();
        g_state.replayIndex = 0;
        g_state.recording = false;
        g_state.playing = false;
        g_state.playbackFinished = false;
        g_state.events.clear();
        g_state.lastMessage = "READY";
        loadTake(layer, false);
    }

    void processReplayFrame(PlayLayer* layer) {
        if (!g_state.playing || !layer) return;

        while (g_state.replayIndex < g_state.events.size() &&
               g_state.events[g_state.replayIndex].frame <= g_state.frame) {
            auto const event = g_state.events[g_state.replayIndex++];
            auto player = playerFor(layer, event.player);
            if (!player) continue;

            if (event.pressed) {
                player->pushButton(PlayerButton::Jump);
                g_state.replayHeld[event.player] = true;
            } else {
                player->releaseButton(PlayerButton::Jump);
                g_state.replayHeld[event.player] = false;
            }
        }

        if (g_state.replayIndex >= g_state.events.size()) {
            stopReplay(layer, "REPLAY FINISHED");
            g_state.playbackFinished = true;
        }
    }
}

class $modify(FairFramePlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        startLevel(this);
        return true;
    }

    void postUpdate(float dt) {
        if (g_state.activeLayer != this) startLevel(this);
        processReplayFrame(this);
        PlayLayer::postUpdate(dt);
        ++g_state.frame;
        updateHud(this);
    }

    void resetLevel() {
        if (g_state.recording) {
            g_state.recording = false;
            if (!g_state.events.empty()) exportTake(this, true);
            g_state.lastMessage = "TAKE SAVED ON RESET";
        }
        if (g_state.playing) stopReplay(this, "REPLAY STOPPED");
        PlayLayer::resetLevel();
        g_state.frame = 0;
        ++g_state.attempt;
        g_state.previousEventFrame = -1;
        g_state.previousEventAttempt = g_state.attempt;
        g_state.pressStartedAt.clear();
        g_state.replayIndex = 0;
    }

    void levelComplete() {
        if (g_state.recording) {
            g_state.recording = false;
            exportTake(this, true);
            g_state.lastMessage = "TAKE SAVED";
        }
        if (g_state.playing) stopReplay(this, "LEVEL COMPLETE");
        PlayLayer::levelComplete();
    }

    void onQuit() {
        if (g_state.recording) {
            g_state.recording = false;
            if (!g_state.events.empty()) exportTake(this, true);
        }
        if (g_state.playing) stopReplay(this, "REPLAY STOPPED");
        removeHud();
        g_state.activeLayer = nullptr;
        PlayLayer::onQuit();
    }
};

class $modify(FairFrameGameLayer, GJBaseGameLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (PlayLayer::get() == this) {
            if (g_state.recording) {
                g_state.recording = false;
                if (!g_state.events.empty()) exportTake(PlayLayer::get(), true);
                g_state.lastMessage = "TAKE SAVED ON DEATH";
            }
            if (g_state.playing) {
                g_state.playing = false;
                g_state.replayHeld.clear();
                g_state.lastMessage = "REPLAY STOPPED ON DEATH";
            }
        }
        GJBaseGameLayer::destroyPlayer(player, object);
    }
};

class $modify(FairFramePlayerObject, PlayerObject) {
    bool pushButton(PlayerButton button) {
        const bool handled = PlayerObject::pushButton(button);
        if (handled && button == PlayerButton::Jump) recordInput(true, this);
        return handled;
    }

    bool releaseButton(PlayerButton button) {
        const bool handled = PlayerObject::releaseButton(button);
        if (handled && button == PlayerButton::Jump) recordInput(false, this);
        return handled;
    }
};

class $modify(FairFramePauseLayer, PauseLayer) {
    struct Fields {
        CCLabelBMFont* status = nullptr;
        CCLabelBMFont* eventsList = nullptr;
        ButtonSprite* recordSprite = nullptr;
        ButtonSprite* replaySprite = nullptr;
        ButtonSprite* hudSprite = nullptr;
    };

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        constexpr float panelWidth = 296.f;
        constexpr float panelHeight = 276.f;

        auto panel = CCLayerColor::create(ccc4(8, 16, 32, 238), panelWidth, panelHeight);
        panel->setPosition({14.f, winSize.height - panelHeight - 14.f});
        panel->setID("fairframe-pause-panel"_spr);

        auto accent = CCLayerColor::create(ccc4(68, 224, 255, 255), panelWidth, 3.f);
        accent->setPosition({0.f, panelHeight - 3.f});
        panel->addChild(accent, 2);

        auto title = CCLabelBMFont::create("FAIRFRAME", "bigFont.fnt");
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({14.f, panelHeight - 24.f});
        title->setScale(0.62f);
        title->setColor(ccc3(88, 228, 255));
        panel->addChild(title);

        auto subtitle = CCLabelBMFont::create("FREE  /  OFFLINE  /  INPUT REPLAY", "chatFont.fnt");
        subtitle->setAnchorPoint({0.f, 0.5f});
        subtitle->setPosition({15.f, panelHeight - 43.f});
        subtitle->setScale(0.43f);
        subtitle->setColor(ccc3(150, 178, 210));
        panel->addChild(subtitle);

        auto status = CCLabelBMFont::create(statusText().c_str(), "chatFont.fnt");
        status->setAnchorPoint({0.f, 0.5f});
        status->setPosition({15.f, panelHeight - 65.f});
        status->setScale(0.47f);
        status->setColor(ccc3(239, 246, 255));
        status->setID("fairframe-status"_spr);
        panel->addChild(status);
        m_fields->status = status;

        auto menu = CCMenu::create();
        menu->setPosition({panelWidth * 0.5f, 139.f});
        menu->setID("fairframe-control-menu"_spr);

        auto recordSprite = ButtonSprite::create("RECORD / STOP", "bigFont.fnt", "GJ_button_01.png", 0.68f);
        recordSprite->setScale(0.50f);
        m_fields->recordSprite = recordSprite;
        auto recordButton = CCMenuItemSpriteExtra::create(
            recordSprite, this, menu_selector(FairFramePauseLayer::onToggleRecording)
        );
        recordButton->setPosition({-68.f, 35.f});
        recordButton->setID("fairframe-record-button"_spr);
        menu->addChild(recordButton);

        auto replaySprite = ButtonSprite::create("REPLAY LAST TAKE", "bigFont.fnt", "GJ_button_02.png", 0.68f);
        replaySprite->setScale(0.50f);
        m_fields->replaySprite = replaySprite;
        auto replayButton = CCMenuItemSpriteExtra::create(
            replaySprite, this, menu_selector(FairFramePauseLayer::onToggleReplay)
        );
        replayButton->setPosition({68.f, 35.f});
        replayButton->setID("fairframe-replay-button"_spr);
        menu->addChild(replayButton);

        auto exportSprite = ButtonSprite::create("EXPORT CSV", "bigFont.fnt", "GJ_button_02.png", 0.68f);
        exportSprite->setScale(0.50f);
        auto exportButton = CCMenuItemSpriteExtra::create(
            exportSprite, this, menu_selector(FairFramePauseLayer::onExportCsv)
        );
        exportButton->setPosition({-68.f, -4.f});
        exportButton->setID("fairframe-export-button"_spr);
        menu->addChild(exportButton);

        auto clearSprite = ButtonSprite::create("CLEAR TAKE", "bigFont.fnt", "GJ_button_06.png", 0.68f);
        clearSprite->setScale(0.50f);
        auto clearButton = CCMenuItemSpriteExtra::create(
            clearSprite, this, menu_selector(FairFramePauseLayer::onClearTake)
        );
        clearButton->setPosition({68.f, -4.f});
        clearButton->setID("fairframe-clear-button"_spr);
        menu->addChild(clearButton);

        auto hudSprite = ButtonSprite::create(
            g_state.showHud ? "HUD: ON" : "HUD: OFF",
            "bigFont.fnt", "GJ_button_04.png", 0.68f
        );
        hudSprite->setScale(0.50f);
        m_fields->hudSprite = hudSprite;
        auto hudButton = CCMenuItemSpriteExtra::create(
            hudSprite, this, menu_selector(FairFramePauseLayer::onToggleHud)
        );
        hudButton->setPosition({0.f, -43.f});
        hudButton->setID("fairframe-hud-button"_spr);
        menu->addChild(hudButton);

        panel->addChild(menu, 3);

        auto eventsList = CCLabelBMFont::create(recentEventsText().c_str(), "chatFont.fnt");
        eventsList->setAnchorPoint({0.f, 1.f});
        eventsList->setPosition({14.f, 66.f});
        eventsList->setScale(0.43f);
        eventsList->setColor(ccc3(204, 222, 244));
        eventsList->setID("fairframe-recent-events"_spr);
        panel->addChild(eventsList);
        m_fields->eventsList = eventsList;

        auto hint = CCLabelBMFont::create("Replay restarts from the level start.", "chatFont.fnt");
        hint->setAnchorPoint({0.f, 0.5f});
        hint->setPosition({14.f, 12.f});
        hint->setScale(0.39f);
        hint->setColor(ccc3(140, 164, 194));
        panel->addChild(hint);

        this->addChild(panel, 100);
    }

    void onToggleRecording(CCObject*) {
        auto layer = PlayLayer::get();
        if (!layer) {
            FLAlertLayer::create("FairFrame", "Open a level before recording.", "OK")->show();
            return;
        }

        if (g_state.recording) {
            g_state.recording = false;
            if (!g_state.events.empty()) {
                exportTake(layer, true);
                g_state.lastMessage = "TAKE SAVED";
            } else {
                g_state.lastMessage = "NO INPUTS RECORDED";
            }
        } else {
            if (g_state.playing) stopReplay(layer, "REPLAY STOPPED");
            beginRecording();
        }
        refreshStatus();
    }

    void onToggleReplay(CCObject*) {
        auto layer = PlayLayer::get();
        if (!layer) {
            FLAlertLayer::create("FairFrame", "Open a level before starting replay.", "OK")->show();
            return;
        }

        if (g_state.playing) {
            stopReplay(layer, "REPLAY STOPPED");
            refreshStatus();
            return;
        }

        if (g_state.recording) {
            g_state.recording = false;
            if (!g_state.events.empty()) exportTake(layer, true);
        }

        if (g_state.events.empty()) loadTake(layer, false);
        if (g_state.events.empty()) {
            FLAlertLayer::create("FairFrame", "No take is available for this level yet. Record a run first.", "OK")->show();
            refreshStatus();
            return;
        }

        g_state.playing = false;
        releaseReplayButtons(layer);
        layer->resetLevel();

        g_state.frame = 0;
        g_state.attempt = 1;
        g_state.replayIndex = 0;
        g_state.replayHeld.clear();
        g_state.playbackFinished = false;
        g_state.recording = false;
        g_state.playing = true;
        g_state.lastMessage = "REPLAY STARTED";
        this->onResume(nullptr);
    }

    void onExportCsv(CCObject*) {
        exportTake(PlayLayer::get(), false);
        refreshStatus();
    }

    void onClearTake(CCObject*) {
        auto layer = PlayLayer::get();
        g_state.recording = false;
        if (g_state.playing) stopReplay(layer, "REPLAY STOPPED");
        g_state.events.clear();
        g_state.pressStartedAt.clear();
        g_state.replayIndex = 0;
        g_state.previousEventFrame = -1;
        g_state.previousEventAttempt = g_state.attempt;
        g_state.lastMessage = "TAKE CLEARED";

        if (layer) {
            std::error_code ec;
            std::filesystem::remove(takePath(layer), ec);
        }
        refreshStatus();
    }

    void onToggleHud(CCObject*) {
        g_state.showHud = !g_state.showHud;
        if (m_fields->hudSprite) {
            m_fields->hudSprite->setString(g_state.showHud ? "HUD: ON" : "HUD: OFF");
        }
        if (!g_state.showHud) removeHud();
        g_state.lastMessage = g_state.showHud ? "HUD ENABLED" : "HUD HIDDEN";
        refreshStatus();
    }

    void refreshStatus() {
        if (m_fields->status) m_fields->status->setString(statusText().c_str());
        if (m_fields->eventsList) m_fields->eventsList->setString(recentEventsText().c_str());
    }
};
