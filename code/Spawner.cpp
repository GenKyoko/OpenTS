/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Multiplayer spawner class.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "win.h"

#include "spawner.h"

#include "_map.h"
#include "_rules.h"
#include "_surface.h"
#include "addon.h"
#include "campaign.h"
#include "ccini.h"
#include "data.h"
#include "dbgprint.h"
#include "goptions.h"
#include "globals.h"
#include "gscreen.h"
#include "house.h"
#include "houstype.h"
#include "init.h"
#include "ipxmgr.h"
#include "language/language.h"
#include "loaddlg.h"
#include "map.h"
#include "mouse.h"
#include "netdlg.h"
#include "ownrdraw.h"
#include "point.h"
#include "rules.h"
#include "saveload.h"
#include "scenario.h"
#include "techno.h"
#include "scheme.h"
#include "session.h"
#include "wspudp.h"
#include "wwmouse.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>

bool Spawner::HasSpawned = false;
bool Spawner::SpawnRequested = false;
std::unique_ptr<SpawnerConfig> Spawner::Config;


/**
 *  Records that the launcher asked for a spawned session (-SPAWN on the
 *  command line). The session itself only starts once SPAWN.INI is read.
 */
void Spawner::Request_Spawn(void)
{
	SpawnRequested = true;
}


/**
 *  Was a spawned session requested on the command line?
 */
bool Spawner::Is_Requested(void)
{
	return SpawnRequested;
}


/**
 *  Initializes the Spawner.
 *
 *  @author: ZivDero
 */
bool Spawner::Init()
{
	Config = std::make_unique<SpawnerConfig>();

	CCFileClass spawn_file("SPAWN.INI");
	CCINIClass spawn_ini;

	if (spawn_file.Is_Available()) {
		spawn_ini.Load(spawn_file, false);
		Config->Read_INI(spawn_ini);
		return true;
	}

	DebugFmt("SPAWN.INI not found!\n");
	Config.reset();
	return false;
}


/**
 *  Starts the game.
 *
 *  @author: ZivDero
 */
bool Spawner::Start_Game()
{
	if (HasSpawned) {
		return false;
	}

	GameActive = true;

	/**
	 *  Initialize some OD global state so that dialogs work correctly.
	 */
	OwnerDraw::Initialize();

	/**
	 *  Clear the screen before Start_Scenario just in case so that it has
	 *  a clean slate to work with.
	 */
	HiddenSurface->Fill(TBLACK);
	Update_Visible_Surface();

	DebugFmt("[Spawner] Start_Game: Starting scenario {}\n", Config->ScenarioName);
	const bool result = Start_Scenario(Config->ScenarioName.data());
	HasSpawned = true;

	if (!result) {
		DebugFmt("[Spawner] Start_Game: Start_Scenario returned false!\n");
		return result;
	}

	/**
	 *  Tail of Select_Game: set up the game screen and
	 *  render one frame for the caller to fade in. The unmatched final Hide
	 *  returns the mouse hidden-by-one, which the caller (Main_Game) shows.
	 */
	HiddenSurface->Fill(TBLACK);
	Update_Visible_Surface();
	LogicalSurface = HiddenSurface;

	Show_Mouse();

	Map.Override_Mouse_Shape(MOUSE_NO_MOVE);
	Map.Revert_Mouse_Shape();

	Map.Activate(1);
	Map.Flag_To_Redraw();

	Hide_Mouse();

	return result;
}

int Spawner::Spawner_Config_AI_Difficulty_To_Game_AI_Difficulty(int difficulty)
{
	switch (difficulty) {
	case 0:
		return DIFF_EASY;
	case 1:
		return DIFF_NORMAL;
	case 2:
		return DIFF_HARD;
	}

	DebugFmt("Spawner_Config_AI_Difficulty_To_Game_AI_Difficulty: Unknown difficulty level {}", difficulty);
	return -1;
}


/**
 *  Validates config values that are later used as array and heap indices.
 *
 *  @author: ZivDero
 */
bool Spawner::Validate_Config()
{
	if (Config == nullptr || Config->HumanPlayers < 1 || Config->HumanPlayers > MAX_PLAYERS) {
		DebugFmt("[Spawner] Invalid human player count {}.\n", Config ? Config->HumanPlayers : -1);
		return false;
	}

	if (Config->LocalPlayerIndex < 0 || Config->LocalPlayerIndex >= Config->HumanPlayers) {
		DebugFmt("[Spawner] Invalid local player index {}.\n", Config->LocalPlayerIndex);
		return false;
	}

	if (Config->AIPlayers < 0 || Config->AIPlayers > MAX_PLAYERS - Config->HumanPlayers) {
		DebugFmt("[Spawner] Invalid AI player count {} for {} human players.\n", Config->AIPlayers, Config->HumanPlayers);
		return false;
	}

	if (Config->Protocol != 0 && (Config->FrameSendRate < 1 || Config->FrameSendRate > UCHAR_MAX)) {
		DebugFmt("[Spawner] Invalid frame send rate {}. Expected a value from 1 through 255.\n", Config->FrameSendRate);
		return false;
	}

	/**
	 *  When resuming a saved game, the session state comes from the save
	 *  itself and the client only provides a minimal config, so the slot
	 *  data is neither present nor used.
	 */
	if (Config->LoadSaveGame) {
		return true;
	}

	/**
	 *  Campaign scenarios create their houses from the map rather than the
	 *  multiplayer slot configuration.
	 */
	if (Config->IsCampaign) {
		const int human_difficulty = static_cast<int>(Config->CampaignDifficulty);
		const int computer_difficulty = static_cast<int>(Config->CampaignCDifficulty);
		if (human_difficulty < DIFF_FIRST || human_difficulty >= DIFF_COUNT || computer_difficulty < DIFF_FIRST || computer_difficulty >= DIFF_COUNT) {
			DebugFmt("[Spawner] Invalid campaign difficulty values {}, {}.\n", human_difficulty, computer_difficulty);
			return false;
		}

		if (Config->CampaignID != CAMPAIGN_NONE && (Config->CampaignID < CAMPAIGN_FIRST || Config->CampaignID >= Campaigns.Count())) {
			DebugFmt("[Spawner] Invalid campaign ID {}.\n", static_cast<int>(Config->CampaignID));
			return false;
		}

		return true;
	}

	if (Config->AIDifficulty < DIFF_FIRST || Config->AIDifficulty >= DIFF_COUNT) {
		DebugFmt("[Spawner] Invalid global AI difficulty {}.\n", Config->AIDifficulty);
		return false;
	}

	const int total_slots = Config->HumanPlayers + Config->AIPlayers;
	std::vector<bool> colors_used(ColorSchemes.Count());

	for (int slot = 0; slot < total_slots; ++slot) {
		const auto& player = Config->Players[slot];
		const int color = static_cast<int>(player.Color);
		const int house = static_cast<int>(player.House);

		if (color < 0 || color >= static_cast<int>(colors_used.size())) {
			DebugFmt("[Spawner] Slot {} has invalid color index {}.\n", slot, color);
			return false;
		}

		/**
		 *  Only human colors need to be unique; AI houses may share one.
		 */
		if (player.IsHuman) {
			if (colors_used[color]) {
				DebugFmt("[Spawner] Human slot {} reuses color index {}.\n", slot, color);
				return false;
			}
			colors_used[color] = true;
		}

		if (house < 0 || house >= HouseTypes.Count()) {
			DebugFmt("[Spawner] Slot {} has invalid house index {}.\n", slot, house);
			return false;
		}

		if (player.IsHuman) {
			if (player.Name.empty()) {
				DebugFmt("[Spawner] Human slot {} has an empty name.\n", slot);
				return false;
			}

			if (player.Name.size() >= MPLAYER_NAME_MAX) {
				DebugFmt("[Spawner] Human slot {} has a name longer than {} characters.\n", slot, MPLAYER_NAME_MAX - 1);
				return false;
			}

			for (int other_slot = 0; other_slot < slot; ++other_slot) {
				const auto& other = Config->Players[other_slot];
				if (other.IsHuman && !_stricmp(player.Name.c_str(), other.Name.c_str())) {
					DebugFmt("[Spawner] Human slots {} and {} use the same name.\n", other_slot, slot);
					return false;
				}
			}
		}
		else {
			if (player.Difficulty < -1 || player.Difficulty > 6) {
				DebugFmt("[Spawner] AI slot {} has invalid difficulty {}.\n", slot, player.Difficulty);
				return false;
			}
		}

		for (int ally : player.Alliances) {
			if (ally < -1 || ally >= total_slots) {
				DebugFmt("[Spawner] Slot {} has invalid alliance target {}.\n", slot, ally);
				return false;
			}
		}
	}

	return true;
}

/**
 *  Configures session and extension state from the current spawn Config->
 *
 *  @author: ZivDero
 */
bool Spawner::Init_Session(char* scenario_name)
{
	if (!Validate_Config()) {
		return false;
	}

	const auto& local_player = Config->Players[Config->LocalPlayerIndex];

	strcpy_s(Session.ScenarioFileName, sizeof(Session.ScenarioFileName), scenario_name);
	Session.Options.ScenarioIndex = -1;
	Session.Options.Bases = Config->Bases;
	Session.Options.Credits = Config->Credits;
	Session.Options.BridgeDestruction = Config->BridgeDestroy;
	Session.Options.Goodies = Config->Crates;
	Session.Options.ShortGame = Config->ShortGame;
	Session.IsBuildOffAlly = Config->BuildOffAlly;
	Session.Options.GameSpeed = Config->GameSpeed;
	Session.Options.CrapEngineers = Config->MultiEngineer;
	Session.Options.UnitCount = Config->UnitCount;
	Session.Options.AIPlayers = Config->AIPlayers;
	Session.Options.AIDifficulty = (DiffType)Config->AIDifficulty;
	Session.Options.AlliesAllowed = Config->AlliesAllowed;
	Session.Options.HarvTruce = Config->HarvesterTruce;
	Session.Options.FogOfWar = Config->FogOfWar;
	Session.Options.MCVRedeploy = Config->MCVRedeploy;
	std::snprintf(Session.Options.ScenarioDescription, sizeof(Session.Options.ScenarioDescription), "%s", Config->MapName.c_str());
	Session.ColorIdx = local_player.Color;
	Session.NumPlayers = Config->HumanPlayers;

	Seed = Config->Seed;
	BuildLevel = Config->TechLevel;
	Options.GameSpeed = Config->GameSpeed;

	Session.NextCampaignAutoSaveSlot = Config->NextCampaignAutoSaveNumber;
	Session.NextSkirmishAutoSaveSlot = Config->NextSkirmishAutoSaveNumber;

	const auto nodename = new NodeNameType();
	Session.Players.Add(nodename);

	std::snprintf(nodename->Name, sizeof(nodename->Name), "%s", local_player.Name.c_str());
	nodename->Player.House = local_player.House;
	nodename->Player.Color = local_player.Color;
	nodename->Player.ProcessTime = -1;

	if (Config->IsCampaign) {
		Session.Type = GAME_NORMAL;
	}
	else if (Session.NumPlayers > 1) {
		Session.Type = GAME_INTERNET; // HACK: will be set to GAME_IPX later
	}
	else {
		Session.Type = GAME_SKIRMISH;
	}

	Session.IsSpawnerSession = true;
	Session.MultiplayerAutoSaveInterval = Config->AutoSaveInterval;
	Session.IsQuickMatch = Config->QuickMatch;
	Session.IsWriteStatistics = Config->WriteStatistics;
	Session.IsSkipScoreScreen = Config->SkipScoreScreen;
	Session.IsAutoSurrender = Config->AutoSurrender;
	Session.IsAttackNeutralUnits = Config->AttackNeutralUnits;
	Session.IsCoachMode = Config->CoachMode;
	Session.IsContinueWithoutHumans = Config->ContinueWithoutHumans;
	Session.IsScrapMetal = Config->ScrapMetal;
	Session.IsAINamesByDifficulty = Config->AINamesByDifficulty;
	Session.IsPlayMoviesInMultiplayer = Config->PlayMoviesInMultiplayer;
	Session.MultiplayerSavesInitializedForThisSession = Config->LoadSaveGame;

	/*
	 * No host announces itself in a spawned session, so the game master is
	 * left to the engine's own first-human-house rule.
	 */
	Session.MasterPlayerID = -1;
	Session.MasterPlayerName[0] = '\0';

	Session.StatsMapName = Config->MapName.c_str();
	Session.StatsMapHash = Config->MapHash.c_str();
	Session.DifficultyName = Config->DifficultyName.c_str();
	if (!Config->CustomLoadScreen.empty()) {
		Session.CustomLoadScreen = Config->CustomLoadScreen.c_str();
	}
	if (Config->CustomLoadScreenPos.X > 0 && Config->CustomLoadScreenPos.Y > 0) {
		Session.CustomLoadScreenPos = Point2D(Config->CustomLoadScreenPos);
	}

	/**
	 *  NOTE: Scenario data gets cleared between this point and the scenario start, because the first step
	 *  in reading a scenario is calling Clear_Scenario. Assume any scenario variables set here to get cleared.
	 *  Only set up some fields that we need for initialization (like difficulty, which is read by the environment).
	 */
	Scen->Difficulty = Config->CampaignDifficulty;
	Scen->CDifficulty = Config->CampaignCDifficulty;

	const int total_slots = std::min(Config->HumanPlayers + Config->AIPlayers, MAX_PLAYERS);

	for (int slot_index = 0; slot_index < total_slots; ++slot_index) {
		const auto& player_config = Config->Players[slot_index];
		auto& slot_info = Session.SlotInfo[slot_index];

		slot_info.IsConfigured = true;
		slot_info.IsHuman = player_config.IsHuman;
		slot_info.Color = player_config.Color;
		slot_info.House = player_config.House;

		if (!slot_info.IsHuman && player_config.Difficulty >= 0) {
			slot_info.Difficulty = Spawner_Config_AI_Difficulty_To_Game_AI_Difficulty(player_config.Difficulty);

			if (slot_info.Difficulty < 0) {
				return false;
			}
		}

		slot_info.IsObserver = player_config.IsObserver;
		slot_info.SpawnLocation = player_config.SpawnLocation;

		for (int ally_index = 0; ally_index < std::size(slot_info.Alliances); ++ally_index) {
			slot_info.Alliances[ally_index] = player_config.Alliances[ally_index];
		}
	}

	return true;
}


/**
 *  Applies the starting alliances the launcher dictated for each slot.
 *
 *  Slots name their allies by slot number; the houses those slots were built
 *  into were recorded while the scenario start assigned them. Alliances are
 *  formed in both directions while the scenario is still being set up, which
 *  is the same grace period the engine's own lobby alliance setup enjoys.
 *
 *  @author: OpenTS contributors
 */
static void Apply_Spawned_Alliances(void)
{
	ScenarioInit++;

	for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
		const SessionClass::SpawnerSlotInfoType& slot_info = Session.SlotInfo[slot];

		if (!slot_info.IsConfigured || slot_info.IsObserver || slot_info.HouseID < 0) {
			DebugString("[Spawner] Alliances: slot %d skipped (configured=%d, observer=%d, house=%d)\n",
				slot, slot_info.IsConfigured, slot_info.IsObserver, slot_info.HouseID);
			continue;
		}

		HouseClass* house = Houses[slot_info.HouseID];
		if (house == NULL) {
			continue;
		}

		for (int ally = 0; ally < MAX_PLAYERS; ++ally) {
			int target_slot = slot_info.Alliances[ally];

			if (target_slot < 0 || target_slot >= MAX_PLAYERS || target_slot == slot) {
				continue;
			}

			const SessionClass::SpawnerSlotInfoType& target_info = Session.SlotInfo[target_slot];
			if (!target_info.IsConfigured || target_info.IsObserver || target_info.HouseID < 0) {
				continue;
			}

			HouseClass* ally_house = Houses[target_info.HouseID];
			if (ally_house != NULL) {
				DebugString("[Spawner] Alliances: %s (slot %d, house %d) allied with %s (slot %d, house %d)\n",
					house->IniName.c_str(), slot, slot_info.HouseID,
					ally_house->IniName.c_str(), target_slot, target_info.HouseID);
				house->Make_Ally(ally_house);
				ally_house->Make_Ally(house);
			}
		}
	}

	ScenarioInit--;
}


/**
 *  Starts a new scenario.
 *
 *  @author: ZivDero
 */
bool Spawner::Start_Scenario(char* scenario_name)
{
	/**
	 *  Can't read an unnamed file, bail.
	 */
	if (scenario_name[0] == 0 && !Config->LoadSaveGame) {
		DebugFmt("[Spawner] Failed to read scenario [{}]\n", scenario_name);
		MessageBox(MainWindow, Localize("TXT_UNABLE_READ_SCENARIO"), "Tiberian Sun", MB_OK);

		return false;
	}

	/**
	 *  Turn Firestorm on, if requested.
	 */
	Disable_Addon(ADDON_ANY);
	if (Config->Firestorm) {
		Enable_Addon(ADDON_FIRESTORM);
		Set_Required_Addon(ADDON_FIRESTORM);
	}

	/*
	 * The multiplayer dialogs load the house list before a game may start;
	 * the spawner skips those dialogs, so the houses are read here before the
	 * config is validated against them.
	 */
	Rule->Do_HouseTypes(*RuleINI);
	for (int i = 0; i < HouseTypes.Count(); i++) {
		HouseTypes[i]->Read_INI(*RuleINI);
	}

	if (!Init_Session(scenario_name)) {
		DebugFmt("[Spawner] Init_Session returned false!\n");
		return false;
	}

	const bool load_save_game = Config->LoadSaveGame;
	const CampaignType campaign_id = Config->CampaignID;
	const bool play_movies_in_multiplayer = Session.IsPlayMoviesInMultiplayer;

	char save_game_name[260];
	std::snprintf(save_game_name, sizeof(save_game_name), "%s", Config->SaveGameName.c_str());

	Init_Random();

	/**
	 *  Start the scenario.
	 */
	if (Session.Type == GAME_NORMAL) {
		Session.Options.Goodies = true;
		if (load_save_game) {
			return Load_Game(save_game_name);
		}
		else {
			return ::Start_Scenario(scenario_name, true, campaign_id);
		}
	}
	else if (Session.Type == GAME_SKIRMISH) {
		if (load_save_game) {
			if (!Load_Game(save_game_name)) {
				return false;
			}
		}
		else if (!::Start_Scenario(scenario_name, true, CAMPAIGN_NONE)) {
			return false;
		}

		// A spectator does not field a side: clear whatever the scenario placed
		// for the local house and hand it the whole map to watch instead. The
		// house itself stays (the engine keys its observer view on PlayerPtr).
		if (Config->Players[Config->LocalPlayerIndex].IsObserver && PlayerPtr != NULL) {
			PlayerPtr->IsObserver = true;
			for (int i = 0; i < Technos.Count(); i++) {
				if (Technos[i]->House == PlayerPtr) {
					delete Technos[i];
					i--;
				}
			}
			Map.Reveal_The_Map();
		}

		Apply_Spawned_Alliances();

		return true;
	}
	else {
		if (!Init_Network()) {
			return false;
		}

		bool result = load_save_game ? Load_Game(save_game_name) : ::Start_Scenario(scenario_name, play_movies_in_multiplayer, CAMPAIGN_NONE);
		if (!result) {
			return false;
		}

		Apply_Spawned_Alliances();

		/*
		 * A spawned game starts every player at once; the engine's own lobby
		 * handshake is skipped and the connections are formed straight from
		 * the addresses the launcher provided.
		 */
		Session.Type = GAME_IPX;
		return Session.Create_Connections() != 0;
	}
}


/**
 *  Loads a saved game.
 *
 *  @author: ZivDero
 */
bool Spawner::Load_Game(const char* file_name)
{
	if (file_name == nullptr || file_name[0] == '\0') {
		return false;
	}

	/*
	 * The engine load path validates the save's version stamp and restores
	 * the game type it was saved under.
	 */
	if (!LoadOptionsClass().Load_File(file_name)) {
		DebugFmt("[Spawner] Failed to load savegame [{}]\n", file_name);
		MessageBox(MainWindow, Localize("TXT_ERROR_LOADING_GAME"), Localize("TXT_SHORT_TITLE"), MB_OK);
		return false;
	}

	Scen->IsSkipScore |= Config->SkipScoreScreen;

	return true;
}


/**
 *  Initializes everything necessary for an MP game.
 *
 *  @author: ZivDero
 */
bool Spawner::Init_Network()
{
	/*
	 * The transport is the game's own CnCNet tunnel when the launcher named
	 * one; otherwise the players talk straight to each other at the addresses
	 * SPAWN.INI carries for them.
	 */
	const bool tunnelled = Config->TunnelPort != 0;
	if (tunnelled) {
		Ipx.Configure_Tunnel(htons(static_cast<unsigned short>(Config->TunnelId)),
			inet_addr(Config->TunnelIp.c_str()), htons(static_cast<unsigned short>(Config->TunnelPort)));
	}
	else {
		Ipx.Configure_Direct_Peers(static_cast<unsigned short>(Config->ListenPort));
	}

	/*
	 * Add the remote human players. The local player is already first in
	 * Session.Players. In a tunnelled game a player is addressed by tunnel ID
	 * in the port, with the IP unused; otherwise the address is the player's own.
	 */
	for (int slot_index = 0; slot_index < Config->HumanPlayers; ++slot_index) {
		if (slot_index == Config->LocalPlayerIndex) continue;

		const auto& player = Config->Players[slot_index];

		auto nodename = new NodeNameType();
		Session.Players.Add(nodename);

		std::snprintf(nodename->Name, sizeof(nodename->Name), "%s", player.Name.c_str());
		nodename->Player.House = player.House;
		nodename->Player.Color = player.Color;
		nodename->Player.ProcessTime = -1;
		nodename->Game.LastTime = 1;

		if (tunnelled) {
			nodename->Address.Set_Address(0, htons(static_cast<unsigned short>(player.Port)));
		}
		else {
			nodename->Address.Set_Address(inet_addr(player.Ip.c_str()), htons(static_cast<unsigned short>(player.Port)));
		}

		/*
		 * Global-channel chatter reaches the other players through these
		 * addresses, which the connection layer also sends private packets to.
		 */
		Ipx.Add_Peer(nodename->Address);
	}

	/*
	 * Bring the socket up and start listening before the scenario is read,
	 * so the connections can be formed once the houses exist.
	 */
	if (!::Init_Network()) {
		DebugFmt("[Spawner] Failed to initialize the UDP transport.\n");
		Ipx.Shutdown();
		return false;
	}

	/*
	 * The lobby is skipped, so there is no measured response time to derive
	 * the frame timing from; the launcher's values are used as they stand.
	 */
	Session.FrameSendRate = std::clamp<unsigned int>(Config->FrameSendRate, 1, 255);
	Session.LatencyFudge = 0;
	Session.PrecalcMaxAhead = 0;
	Session.PrecalcDesiredFrameRate = 0;
	Session.MaxAhead = Config->MaxAhead < 0 ? Session.FrameSendRate * 6 : static_cast<unsigned int>(Config->MaxAhead);
	Session.MaxMaxAhead = 0;
	Session.CommProtocol = COMM_PROTOCOL_MULTI_E_COMP;

	Ipx.Set_Timing(60, static_cast<unsigned int>(-1), 600, true);

	return true;
}
