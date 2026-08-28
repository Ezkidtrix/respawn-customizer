#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <Geode/utils/cocos.hpp>
#include <Geode/utils/general.hpp>
#include <cvolton.level-id-api/include/EditorIDs.hpp>

#include <matjson.hpp>

using namespace geode::prelude;

struct Settings {
  bool enabled = true;

  int defaultTime = 1000;
  bool enableDefault = false;
};
static Settings settings;

struct RespawnTime {
  bool enabled = settings.enableDefault;

  std::string id;
  int time = 1000;
};

template<>
struct matjson::Serialize<RespawnTime> {
  static Result<RespawnTime> fromJson(matjson::Value const& val) {
    GEODE_UNWRAP_INTO(bool enabled, val["enabled"].asBool());

    GEODE_UNWRAP_INTO(int time, val["time"].asInt());
    GEODE_UNWRAP_INTO(std::string id, val["id"].asString());
    
    return Ok(RespawnTime{ enabled, id, time });
  }

  static matjson::Value toJson(RespawnTime const& val) {
    auto obj = matjson::Value();
    obj["enabled"] = val.enabled;

    obj["id"] = val.id;
    obj["time"] = val.time;

    return obj;
  }
};

std::string getLevelKey(GJGameLevel* level) {
  if (!level) return "";

  int id = (level->m_levelID > 0) ? level->m_levelID.value() : EditorIDs::getID(level);
  std::string key = std::to_string(id);

  if (level->m_levelType == GJLevelType::Main) key += "-local";
  if (level->m_levelType == GJLevelType::Editor) key += "-editor";
  if (level->m_dailyID > 0) key += "-daily";
  if (level->m_gauntletLevel) key += "-gauntlet";

  return key;
}

RespawnTime getRespawnTime(GJGameLevel* level) {
  return Mod::get()->getSavedValue<RespawnTime>(getLevelKey(level), RespawnTime{ false, getLevelKey(level), settings.defaultTime });
}

class RespawnPopup : public geode::Popup {
protected:
  bool m_enabled = false;
  GJGameLevel* m_level;

  std::string m_key;
  RespawnTime m_respawnTime;

  TextInput* m_input;
  CCMenuItemToggler* m_checkbox;

  bool setup(RespawnPopup* popup, GJGameLevel* level) {
    popup->m_noElasticity = true;
    m_level = level;

    m_key = getLevelKey(m_level);
    m_respawnTime = getRespawnTime(m_level);

    auto winSize = popup->getContentSize();
    auto menu = CCMenu::create();

    menu->setPosition({ 0.5, 0.5 });
    menu->setID("checkbox-menu"); 

    this->setKeyboardEnabled(true);
    this->setTitle("Respawn Time");

    m_checkbox = CCMenuItemToggler::create(
      CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"), CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
      this, menu_selector(RespawnPopup::onToggle)
    );
    m_checkbox->toggle(m_respawnTime.enabled);

    m_checkbox->setAnchorPoint({ 0.5f, 0.5f });
    m_checkbox->setPosition(CCPoint{ winSize.width / 2 - 60.f, winSize.height / 2 - 10.f });

    menu->addChild(m_checkbox);
    popup->addChild(menu);

    m_input = TextInput::create(100.f, "Time (ms)");
    m_input->setString(std::to_string(m_respawnTime.time));

    m_input->setAnchorPoint({ 0.5f, 0.5f });
    m_input->setPosition(CCPoint{ winSize.width / 2 + 10.f, winSize.height / 2 - 10.f });

    m_input->setFilter("0123456789");
    m_input->setMaxCharCount(5);

    popup->addChild(m_input);
    return true;
  }

  void onToggle(CCObject* sender) {
    m_enabled = !m_checkbox->isToggled();
    m_respawnTime.enabled = m_enabled;

    if (m_level) Mod::get()->setSavedValue(m_key, RespawnTime{ m_respawnTime.enabled, m_respawnTime.id, m_respawnTime.time });
  }

  void onClose(CCObject* sender) override {
    Popup::onClose(sender);
    int time = utils::numFromString<int>(m_input->getString()).unwrapOr(settings.defaultTime);

    m_respawnTime.time = std::clamp(time, 0, 10000);
    if (m_level) Mod::get()->setSavedValue(m_key, RespawnTime{ m_respawnTime.enabled, m_respawnTime.id, m_respawnTime.time });
  }
  
public:
  static RespawnPopup* create(GJGameLevel* level) {
    auto ret = new RespawnPopup();

    if (ret->init(180.f, 80.f)) {
      ret->setup(ret, level);
      ret->autorelease();

      return ret;
    }

    delete ret;
    return nullptr;
  }
};

class $modify(MyPlayLayer, PlayLayer) {
  struct Fields {
    bool m_respawn = false;
  };

  void destroyPlayer(PlayerObject* player, GameObject* object) {
    PlayLayer::destroyPlayer(player, object);
    if (!settings.enabled) return;

    auto respawnTime = getRespawnTime(m_level);
    float time = respawnTime.time / 1000.f;

    if (respawnTime.enabled && !m_fields->m_respawn && player->m_isDead && !m_hasCompletedLevel) {
      m_fields->m_respawn = true;

      auto delay = CCDelayTime::create(time);
      auto callback = cocos::CallFuncExt::create([this]() {
        this->resetLevel();
      });

      auto sequence = CCSequence::create(delay, callback, nullptr);
      this->runAction(sequence);
    }
  }

  void resetLevel() {
    PlayLayer::resetLevel();
    m_fields->m_respawn = false;
  }
};

class $modify(MyPauseLayer, PauseLayer) {
  struct Fields {
    CCNode* m_menu;
  };

  void customSetup() {
    PauseLayer::customSetup();
    if (!settings.enabled) return;

    m_fields->m_menu = this->getChildByID("left-button-menu");
    if (!m_fields->m_menu) return;

    this->setupButton();
    m_fields->m_menu->updateLayout();
  }

  void setupButton() {
    if (!settings.enabled || !m_fields->m_menu) return;

    auto spr = ButtonSprite::create("RS");
    auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MyPauseLayer::openPopup));

    btn->setID("respawn-btn"_spr);
    auto options = AxisLayoutOptions::create()->setScaleLimits(0.6f, 1.0f);

    btn->setLayoutOptions(options);
    m_fields->m_menu->addChild(btn);
  }

  void openPopup(CCObject* sender) {
    RespawnPopup::create(PlayLayer::get()->m_level)->show();
  }
};

$on_mod(Loaded) {
  settings.enabled = Mod::get()->getSettingValue<bool>("enabled");

  settings.defaultTime = Mod::get()->getSettingValue<int>("default-time");
  settings.enableDefault = Mod::get()->getSettingValue<bool>("enable-default");

  listenForSettingChanges<bool>("enabled", [](bool value) {
    settings.enabled = value;
  });

  listenForSettingChanges<int>("default-time", [](int value) {
    settings.defaultTime = value;
  });
  listenForSettingChanges<bool>("enable-default", [](bool value) {
    settings.enableDefault = value;
  });
};