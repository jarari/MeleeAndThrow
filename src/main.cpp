#include "SimpleIni.h"
#include "Utilities.h"

using namespace RE;
using std::unordered_map;

CSimpleIniA ini(true, false, false);
PlayerCharacter* p;
PlayerControls* pcon;
PlayerCamera* pcam;
BGSEquipSlot* grenadeSlot;
Setting* fThrowDelay;
uint32_t meleeKey;
uint32_t throwKey;
bool forceUnbind = true;
bool inLooksMenu = false;
bool inScopeMenu = false;

class InputEventReceiverOverride : public BSInputEventReceiver {
public:
	typedef void (InputEventReceiverOverride::* FnPerformInputProcessing)(const InputEvent* a_queueHead);

	void ProcessButtonEvent(ButtonEvent* evn) {
		if (evn->eventType != INPUT_EVENT_TYPE::kButton) {
			if (evn->next)
				ProcessButtonEvent((ButtonEvent*)evn->next);
			return;
		}

		uint32_t id = evn->idCode;
		if (evn->device == INPUT_DEVICE::kMouse)
			id += 0x100;
		if (evn->device == INPUT_DEVICE::kGamepad)
			id += 0x10000;

		if (id == meleeKey || id == throwKey) {
			BSInputEventUser* handler = (BSInputEventUser*)pcon->meleeThrowHandler;
			float minDelay = fThrowDelay->GetFloat();
			if (id == meleeKey && evn->value == 0.f) {
				bool canMelee = pcon->CanPerformAction(DEFAULT_OBJECT::kActionMelee);
				if (canMelee) {
					evn->heldDownSecs = max(minDelay - 0.1f, 0.f);
					*(uint8_t*)((uintptr_t)handler + 0x29) = 1;
					handler->OnButtonEvent(evn);
				}
			}
			else if (id == throwKey) {
				if (p->currentProcess && p->currentProcess->middleHigh) {
					bool canThrow = pcon->CanPerformAction(DEFAULT_OBJECT::kActionThrow);
					if (canThrow) {
						bool hasGrenade = false;
						p->currentProcess->middleHigh->equippedItemsLock.lock();
						for (auto it = p->currentProcess->middleHigh->equippedItems.begin(); it != p->currentProcess->middleHigh->equippedItems.end(); ++it) {
							if (it->equipIndex.index == 2) {
								hasGrenade = true;
							}
						}
						p->currentProcess->middleHigh->equippedItemsLock.unlock();
						if (hasGrenade) {
							evn->heldDownSecs = minDelay + 20.f;
							*(uint8_t*)((uintptr_t)handler + 0x29) = 1;
							handler->OnButtonEvent(evn);
						}
					}
				}
			}
		}

		if (evn->next)
			ProcessButtonEvent((ButtonEvent*)evn->next);
	}

	void HookedPerformInputProcessing(const InputEvent* a_queueHead) {
		if (!UI::GetSingleton()->menuMode && !inLooksMenu && !inScopeMenu && a_queueHead) {
			ProcessButtonEvent((ButtonEvent*)a_queueHead);
		}
		FnPerformInputProcessing fn = fnHash.at(*(uint64_t*)this);
		if (fn) {
			(this->*fn)(a_queueHead);
		}
	}

	void HookSink() {
		uint64_t vtable = *(uint64_t*)this;
		auto it = fnHash.find(vtable);
		if (it == fnHash.end()) {
			FnPerformInputProcessing fn = SafeWrite64Function(vtable, &InputEventReceiverOverride::HookedPerformInputProcessing);
			fnHash.insert(std::pair<uint64_t, FnPerformInputProcessing>(vtable, fn));
		}
	}

	void UnHookSink() {
		uint64_t vtable = *(uint64_t*)this;
		auto it = fnHash.find(vtable);
		if (it == fnHash.end())
			return;
		SafeWrite64Function(vtable, it->second);
		fnHash.erase(it);
	}

protected:
	static unordered_map<uint64_t, FnPerformInputProcessing> fnHash;
};
unordered_map<uint64_t, InputEventReceiverOverride::FnPerformInputProcessing> InputEventReceiverOverride::fnHash;

void RemapMelee() {
	if (!forceUnbind)
		return;

	ControlMap* cm = ControlMap::GetSingleton();
	if (cm) {
		bool meleeBound = false;
		BSTArray<ControlMap::UserEventMapping>& mouseMap =
			cm->controlMaps[(int)UserEvents::INPUT_CONTEXT_ID::kMainGameplay]->deviceMappings[(int)INPUT_DEVICE::kMouse];
		for (auto it = mouseMap.begin(); it != mouseMap.end(); ++it) {
			if (it->inputKey != 0xff) {
				if (it->eventID == "Melee") {
					meleeBound = true;
				}
				_MESSAGE("Mouse %s key %d remappable %d", it->eventID.c_str(), it->inputKey, it->remappable);
			}
		}
		BSTArray<ControlMap::UserEventMapping>& keyboardMap =
			cm->controlMaps[(int)UserEvents::INPUT_CONTEXT_ID::kMainGameplay]->deviceMappings[(int)INPUT_DEVICE::kKeyboard];
		for (auto it = keyboardMap.begin(); it != keyboardMap.end(); ++it) {
			if (it->inputKey != 0xff) {
				if (it->eventID == "Melee") {
					meleeBound = true;
				}
				_MESSAGE("Keyboard %s key %d remappable %d", it->eventID.c_str(), it->inputKey, it->remappable);
			}
		}
		BSTArray<ControlMap::UserEventMapping>& gamepadMap =
			cm->controlMaps[(int)UserEvents::INPUT_CONTEXT_ID::kMainGameplay]->deviceMappings[(int)INPUT_DEVICE::kGamepad];
		for (auto it = gamepadMap.begin(); it != gamepadMap.end(); ++it) {
			if (it->inputKey != 0xff) {
				if (it->eventID == "Melee") {
					meleeBound = true;
				}
				_MESSAGE("Gamepad %s key %d remappable %d", it->eventID.c_str(), it->inputKey, it->remappable);
			}
		}
		if (meleeBound) {
			cm->RemapButton("Melee", INPUT_DEVICE::kKeyboard, 0xff);
			cm->RemapButton("Melee", INPUT_DEVICE::kMouse, 0xff);
			cm->RemapButton("Melee", INPUT_DEVICE::kGamepad, 0xff);
			cm->SaveRemappings();
		}
	}
}

class MenuWatcher : public BSTEventSink<MenuOpenCloseEvent> {
	virtual BSEventNotifyControl ProcessEvent(const MenuOpenCloseEvent& evn, BSTEventSource<MenuOpenCloseEvent>* src) override {
		if (evn.menuName == "ScopeMenu") {
			if (evn.opening) {
				inScopeMenu = true;
			}
			else {
				inScopeMenu = false;
			}
		}
		else if (evn.menuName == "LooksMenu") {
			if (evn.opening) {
				inLooksMenu = true;
			}
			else {
				inLooksMenu = false;
			}
		}
		return BSEventNotifyControl::kContinue;
	}
};

void LoadConfigs() {
	ini.LoadFile("Data\\F4SE\\Plugins\\MeleeAndThrow.ini");
	meleeKey = std::stoi(ini.GetValue("General", "MeleeKey", "0xA4"), 0, 16);
	throwKey = std::stoi(ini.GetValue("General", "ThrowKey", "0x47"), 0, 16);
	forceUnbind = std::stoi(ini.GetValue("General", "ForceUnbind", "0")) > 0;
	ini.Reset();
}

void InitializePlugin() {
	p = PlayerCharacter::GetSingleton();
	pcon = PlayerControls::GetSingleton();
	pcam = PlayerCamera::GetSingleton();
	grenadeSlot = (BGSEquipSlot*)TESForm::GetFormByID(0x46AAC);
	for (auto it = INISettingCollection::GetSingleton()->settings.begin(); it != INISettingCollection::GetSingleton()->settings.end(); ++it) {
		if ((*it)->GetKey() == "fThrowDelay:Controls") {
			fThrowDelay = *it;
		}
	}
	if (fThrowDelay) {
		_MESSAGE("fThrowDelay:Controls found");
		((InputEventReceiverOverride*)((uint64_t)pcam + 0x38))->HookSink();
	}
	MenuWatcher* mw = new MenuWatcher();
	UI::GetSingleton()->GetEventSource<MenuOpenCloseEvent>()->RegisterSink(mw);
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface * a_f4se, F4SE::PluginInfo * a_info) {
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::warn);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);

	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	if (a_f4se->IsEditor()) {
		logger::critical(FMT_STRING("loaded in editor"));
		return false;
	}

	const auto ver = a_f4se->RuntimeVersion();
	if (ver < F4SE::RUNTIME_1_10_162) {
		logger::critical(FMT_STRING("unsupported runtime v{}"), ver.string());
		return false;
	}

	return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface * a_f4se) {
	F4SE::Init(a_f4se);

	const F4SE::MessagingInterface* message = F4SE::GetMessagingInterface();
	message->RegisterListener([](F4SE::MessagingInterface::Message* msg) -> void {
		if (msg->type == F4SE::MessagingInterface::kGameDataReady) {
			InitializePlugin();
			RemapMelee();
			LoadConfigs();
		}
		else if (msg->type == F4SE::MessagingInterface::kPreLoadGame) {
			LoadConfigs();
		}
		else if (msg->type == F4SE::MessagingInterface::kNewGame) {
			LoadConfigs();
		}
	});

	return true;
}
