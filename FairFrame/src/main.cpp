#include <Geode/Geode.hpp>
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
        int deltaFrames = 0;
        int holdFrames = -1;
        bool pressed = true;
    };

    struct FairFrameState {
        PlayLayer* activeLayer = nullptr;
        CCLayerColor* hudPanel = nullptr;
        CCLabelBMFont* hudLabel = nullptr;
        int frame = 0;
        int attempt = 1;
        int previousEventFrame = -1;
        int previousEventAttempt = 1;
        bool recording = false;
        bool showHud = true;
        std::vector<FrameEvent> events;
        std::map<int, int> pressStartedAt;
    };

    FairFrameState g_state;

    int pressCount() {
        return static_cast<int>(std::count_if(
            g_state.events.begin(), g_state.events.end(),
            [](FrameEvent const& event) { return event.pressed; }
        ));
    }

    std::string lastEventText() {
        if (g_state.events.empty()) return "LAST: --";
        auto const& event = g_state.events.back();
        std::ostringstream out;
        out << "LAST: " << (event.pressed ? "PRESS" : "RELEASE")
            << " @" << event.frame << "  DELTA " << event.deltaFrames;
        if (!event.pressed && event.holdFrames >= 0) out << "  HOLD " << event.holdFrames;
        return out.str();
    }

    std::string statusText() {
        std::ostringstream out;
        out << (g_state.recording ? "REC" : "READY")
            << "  |  ATT " << g_state.attempt
            << "  |  FRAME " << g_state.frame
            << "  |  CLICKS " << pressCount()
            << "  |  EVENTS " << g_state.events.size();
        return out.str();
    }

    std::string recentEventsText() {
        if (g_state.events.empty()) return "RECENT INPUTS\nNo events recorded yet";
        std::ostringstream out;
        out << "RECENT INPUTS";
        const size_t first = g_state.events.size() > 5 ? g_state.events.size() - 5 : 0;
        for (size_t i = first; i < g_state.events.size(); ++i) {
            auto const& event = g_state.events[i];
            out << "\nA" << event.attempt << "  F" << event.frame << "  "
                << (event.pressed ? "PRESS" : "RELEASE") << "  d" << event.deltaFrames;
            if (!event.pressed && event.holdFrames >= 0) out << "  h" << event.holdFrames;
        }
        return out.str();
    }

    std::string sanitizeFilename(std::string value) {
        for (char& c : value) {
            const auto uc = static_cast<unsigned char>(c);
            if (!(std::isalnum(uc) || c == '-' || c == '_')) c = '_';
        }
        if (value.empty()) value = "Unknown-Level";
        return value;
    }

    std::string currentLevelName(PlayLayer* layer) {
        if (!layer || !layer->m_level) return "Unknown-Level";
        std::string name = layer->m_level->m_levelName;
        return name.empty() ? "Unknown-Level" : name;
    }

    std::filesystem::path csvPath(PlayLayer* layer) {
        return Mod::get()->getSaveDir() /
            ("FairFrame-" + sanitizeFilename(currentLevelName(layer)) + "-latest.csv");
    }

    bool exportCsv(PlayLayer* layer, bool quiet = false) {
        if (g_state.events.empty()) {
            if (!quiet) {
                FLAlertLayer::create(
                    "FairFrame",
                    "There are no recorded inputs yet. Start a recording, play, then export again.",
                    "OK"
                )->show();
            }
            return false;
        }

        std::error_code ec;
        const auto folder = Mod::get()->getSaveDir();
        std::filesystem::create_directories(folder, ec);

        const auto path = csvPath(layer);
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file) {
            log::error("FairFrame could not write {}", path.string());
            if (!quiet) {
                FLAlertLayer::create(
                    "FairFrame",
                    "Could not export the CSV. Check that the Geode save folder is writable.",
                    "OK"
                )->show();
            }
            return false;
        }

        file << "# FairFrame input timing export\n";
        file << "# Level: " << currentLevelName(layer) << "\n";
        file << "# Frame values are captured from PlayLayer update ticks.\n";
        file << "attempt,frame,action,delta_frames,hold_frames\n";
        for (auto const& event : g_state.events) {
            file << event.attempt << ','
                 << event.frame << ','
                 << (event.pressed ? "PRESS" : "RELEASE") << ','
                 << event.deltaFrames << ',';
            if (event.holdFrames >= 0) file << event.holdFrames;
            file << '\n';
        }
        file.close();

        log::info("FairFrame exported {} events to {}", g_state.events.size(), path.string());
        if (!quiet) {
            FLAlertLayer::create(
                "FairFrame — Export complete",
                ("Saved " + std::to_string(g_state.events.size()) + " input events to:\n" + path.string()).c_str(),
                "Nice"
            )->show();
        }
        return true;
    }

    void removeHud() {
        if (g_state.hudPanel) {
            g_state.hudPanel->removeFromParentAndCleanup(true);
        }
        g_state.hudPanel = nullptr;
        g_state.hudLabel = nullptr;
    }

    void createHud(PlayLayer* layer) {
        if (!layer || !g_state.showHud || g_state.hudPanel) return;

        const auto size = CCDirector::sharedDirector()->getWinSize();
        auto panel = CCLayerColor::create(ccc4(9, 15, 30, 210), 226.f, 76.f);
        if (!panel) return;
        panel->setAnchorPoint({0.f, 1.f});
        panel->setPosition({12.f, size.height - 12.f});
        panel->setID("fairframe-hud-panel"_spr);

        auto label = CCLabelBMFont::create("FAIRFRAME", "bigFont.fnt");
        if (!label) return;
        label->setAnchorPoint({0.f, 1.f});
        label->setPosition({9.f, 68.f});
        label->setScale(0.34f);
        label->setColor(ccc3(93, 231, 255));
        panel->addChild(label);

        layer->addChild(panel, 10000);
        g_state.hudPanel = panel;
        g_state.hudLabel = label;
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
        if (g_state.hudLabel) {
            std::ostringstream text;
            text << (g_state.recording ? "● REC" : "○ READY")
                 << "\nATT " << g_state.attempt << "   FRAME " << g_state.frame
                 << "\nCLICKS " << pressCount() << "   EVENTS " << g_state.events.size()
                 << "\n" << lastEventText();
            g_state.hudLabel->setString(text.str().c_str());
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
        g_state.events.clear();
        g_state.recording = false;
    }

    void recordInput(bool pressed) {
        if (!g_state.recording || !PlayLayer::get()) return;

        constexpr int jumpKey = static_cast<int>(PlayerButton::Jump);
        int holdFrames = -1;
        if (pressed) {
            g_state.pressStartedAt[jumpKey] = g_state.frame;
        } else {
            auto it = g_state.pressStartedAt.find(jumpKey);
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

    void beginRecording() {
        g_state.events.clear();
        g_state.pressStartedAt.clear();
        g_state.frame = 0;
        g_state.attempt = 1;
        g_state.previousEventFrame = -1;
        g_state.previousEventAttempt = 1;
        g_state.recording = true;
    }
}

class $modify(FairFramePlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        startLevel(this);
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (g_state.activeLayer != this) startLevel(this);
        ++g_state.frame;
        updateHud(this);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        g_state.frame = 0;
        ++g_state.attempt;
        g_state.previousEventFrame = -1;
        g_state.previousEventAttempt = g_state.attempt;
        g_state.pressStartedAt.clear();
    }

    void levelComplete() {
        if (g_state.recording) {
            g_state.recording = false;
            exportCsv(this, true);
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        if (g_state.recording) g_state.recording = false;
        if (!g_state.events.empty()) exportCsv(this, true);
        removeHud();
        g_state.activeLayer = nullptr;
        PlayLayer::onQuit();
    }
};

class $modify(FairFramePlayerObject, PlayerObject) {
    bool pushButton(PlayerButton button) {
        const bool handled = PlayerObject::pushButton(button);
        if (handled && button == PlayerButton::Jump) recordInput(true);
        return handled;
    }

    bool releaseButton(PlayerButton button) {
        const bool handled = PlayerObject::releaseButton(button);
        if (handled && button == PlayerButton::Jump) recordInput(false);
        return handled;
    }
};

class $modify(FairFramePauseLayer, PauseLayer) {
    struct Fields {
        ButtonSprite* recordSprite = nullptr;
        CCLabelBMFont* status = nullptr;
        CCLabelBMFont* eventsList = nullptr;
    };

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({winSize.width - 94.f, winSize.height - 28.f});
        menu->setAnchorPoint({1.f, 1.f});
        menu->setID("fairframe-controls"_spr);

        auto title = CCLabelBMFont::create("FAIRFRAME", "bigFont.fnt");
        title->setScale(0.58f);
        title->setColor(ccc3(93, 231, 255));
        title->setAnchorPoint({0.5f, 0.5f});
        title->setPosition({0.f, 22.f});
        menu->addChild(title);

        const bool recording = g_state.recording;
        auto recordSprite = ButtonSprite::create(
            recording ? "Stop Recording" : "Start Recording",
            "bigFont.fnt",
            recording ? "GJ_button_06.png" : "GJ_button_01.png",
            0.62f
        );
        recordSprite->setScale(0.48f);
        m_fields->recordSprite = recordSprite;
        auto recordButton = CCMenuItemSpriteExtra::create(
            recordSprite, this, menu_selector(FairFramePauseLayer::onToggleRecording)
        );
        recordButton->setPosition({0.f, 0.f});
        menu->addChild(recordButton);

        auto exportSprite = ButtonSprite::create("Export CSV", "bigFont.fnt", "GJ_button_02.png", 0.62f);
        exportSprite->setScale(0.48f);
        auto exportButton = CCMenuItemSpriteExtra::create(
            exportSprite, this, menu_selector(FairFramePauseLayer::onExportCsv)
        );
        exportButton->setPosition({0.f, -34.f});
        menu->addChild(exportButton);

        auto clearSprite = ButtonSprite::create("Clear Session", "bigFont.fnt", "GJ_button_06.png", 0.62f);
        clearSprite->setScale(0.48f);
        auto clearButton = CCMenuItemSpriteExtra::create(
            clearSprite, this, menu_selector(FairFramePauseLayer::onClearSession)
        );
        clearButton->setPosition({0.f, -68.f});
        menu->addChild(clearButton);

        auto hudSprite = ButtonSprite::create(
            g_state.showHud ? "HUD: ON" : "HUD: OFF",
            "bigFont.fnt", "GJ_button_04.png", 0.62f
        );
        hudSprite->setScale(0.48f);
        auto hudButton = CCMenuItemSpriteExtra::create(
            hudSprite, this, menu_selector(FairFramePauseLayer::onToggleHud)
        );
        hudButton->setPosition({0.f, -102.f});
        menu->addChild(hudButton);

        auto status = CCLabelBMFont::create(statusText().c_str(), "chatFont.fnt");
        status->setScale(0.54f);
        status->setAnchorPoint({1.f, 0.5f});
        status->setPosition({-8.f, -132.f});
        status->setID("fairframe-status"_spr);
        menu->addChild(status);
        m_fields->status = status;

        auto eventsList = CCLabelBMFont::create(recentEventsText().c_str(), "chatFont.fnt");
        eventsList->setScale(0.48f);
        eventsList->setAnchorPoint({1.f, 1.f});
        eventsList->setPosition({-8.f, -151.f});
        eventsList->setColor(ccc3(205, 220, 240));
        eventsList->setID("fairframe-recent-events"_spr);
        menu->addChild(eventsList);
        m_fields->eventsList = eventsList;

        this->addChild(menu, 100);
    }

    void onToggleRecording(CCObject*) {
        if (g_state.recording) {
            g_state.recording = false;
        } else {
            beginRecording();
        }
        if (m_fields->recordSprite) {
            m_fields->recordSprite->setString(g_state.recording ? "Stop Recording" : "Start Recording");
            m_fields->recordSprite->updateSprite();
        }
        refreshStatus();
    }

    void onExportCsv(CCObject*) {
        exportCsv(PlayLayer::get(), false);
        refreshStatus();
    }

    void onClearSession(CCObject*) {
        g_state.events.clear();
        g_state.pressStartedAt.clear();
        g_state.previousEventFrame = -1;
        g_state.previousEventAttempt = g_state.attempt;
        refreshStatus();
        FLAlertLayer::create("FairFrame", "The current session has been cleared.", "OK")->show();
    }

    void onToggleHud(CCObject* sender) {
        g_state.showHud = !g_state.showHud;
        auto button = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto sprite = static_cast<ButtonSprite*>(button->getNormalImage());
        sprite->setString(g_state.showHud ? "HUD: ON" : "HUD: OFF");
        sprite->updateSprite();
        if (!g_state.showHud) removeHud();
        refreshStatus();
    }

    void refreshStatus() {
        if (m_fields->status) m_fields->status->setString(statusText().c_str());
        if (m_fields->eventsList) m_fields->eventsList->setString(recentEventsText().c_str());
    }
};
