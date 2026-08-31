StringTable {
 Item "GBRS-RadarStation_US_Name" {
  "US Ground Radar Station (AN/TPN-19)"
 }
 Item "GBRS-RadarStation_USSR_Name" {
  "USSR Ground Radar Station (RPL-5)"
 }
 Item "GBRS-RadarStation_US_Desc" {
  "Placeable US approach radar station with active search and track."
 }
 Item "GBRS-RadarStation_USSR_Desc" {
  "Placeable USSR approach radar station with active search and track."
 }
 Item "GBRS-FieldManual_Entry_QuickStart_Title" {
  "Ground Radar - Quick Start"
 }
 Item "GBRS-FieldManual_Entry_QuickStart_Text_01" {
  "GBRS turns the official approach-radar meshes into buildable Conflict / Game Master ground stations. Detection runs through Radar Development Framework (RDF)."
 }
 Item "GBRS-FieldManual_Entry_QuickStart_Text_02" {
  "1. Place from a construction truck or base build menu (sergeant, cost 450). A work pad appears first. Finish shoveling (build value 250). From a base terminal you also spend 100 HQ composition points."
 }
 Item "GBRS-FieldManual_Entry_QuickStart_Text_03" {
  "2. At the generator: power ON (needs supply). At the desk console: Operate Console to light the PPI / CONTACT / OPTICS CRTs."
 }
 Item "GBRS-FieldManual_Entry_QuickStart_Text_04" {
  "3. Tune your handheld to RADAR NET (US 45.6 MHz / USSR 39.6 MHz) to hear air and counter-battery voice."
 }
 Item "GBRS-FieldManual_Entry_QuickStart_Text_05" {
  "US: AN/TPN-19 look, 12 km air search, 8 km WLR. USSR: Tesla RPL-5 look, 16 km air search, 10 km WLR."
 }
 Item "GBRS-FieldManual_Entry_Workstation_Title" {
  "Ground Radar - Workstation"
 }
 Item "GBRS-FieldManual_Entry_Workstation_Text_01" {
  "Open the console only while the station is powered and you are friendly to its faction. Use Operate Console at the desk — picture is on the world CRTs."
 }
 Item "GBRS-FieldManual_Entry_Workstation_Text_02" {
  "PD SEARCH - pulse-Doppler air search. Skin returns only: anonymous azimuth, range, altitude, speed, SNR. No aircraft type names. Radiating radars show as EMIT. Infantry never paints."
 }
 Item "GBRS-FieldManual_Entry_Workstation_Text_03" {
  "WLR - weapon-locating radar for shells. Full-circle search cues a track, then the dish auto-narrows about ±40° for launch / impact solutions (LCH orange, IMP cyan)."
 }
 Item "GBRS-FieldManual_Entry_Workstation_Text_04" {
  "LOCK - auto-acquires nearby vehicles; click a PPI track to designate a lock. MANUAL - range, scan rate, elevation boresight, and stare bearing knobs."
 }
 Item "GBRS-FieldManual_Entry_Workstation_Text_05" {
  "Controls at the console: Up / Down zoom PPI range. Tab / Cycle Mode cycles modes. Select locks nearest confirmed track. Back leaves. Toggle Optics enables the third CRT (off by default)."
 }
 Item "GBRS-FieldManual_Entry_RadarNet_Title" {
  "Ground Radar - RADAR NET"
 }
 Item "GBRS-FieldManual_Entry_RadarNet_Text_01" {
  "A powered station covering a friendly Conflict base transmits new air contacts and in-radius WLR impacts on a locked intel net."
 }
 Item "GBRS-FieldManual_Entry_RadarNet_Text_02" {
  "US 45.6 MHz (45600 kHz) | USSR 39.6 MHz (39600 kHz) | channel name RADAR NET. You must tune the net to hear ACP voice or RADAR CONTACT tips."
 }
 Item "GBRS-FieldManual_Entry_RadarNet_Text_03" {
  "Local handheld range is about 2 km. Farther coverage uses friendly Conflict HQ radios or powered RelayTransceiver hops (up to 24). Handhelds themselves do not hop."
 }
 Item "GBRS-FieldManual_Entry_RadarNet_Text_04" {
  "Voice: air = grid / heading / altitude; WLR = launch grid / impact grid / ETA. After construction, same-faction players get a one-shot tune briefing. Do not use platoon nets (US 48.0 / USSR 42.0)."
 }
 Item "GBRS-FieldManual_Entry_Supply_Title" {
  "Ground Radar - Supply & Capture"
 }
 Item "GBRS-FieldManual_Entry_Supply_Text_01" {
  "While powered the station drains 15 supply every 25 seconds. Order: covering friendly Conflict base stockpile, then local bunker (400), then a still-linked construction truck."
 }
 Item "GBRS-FieldManual_Entry_Supply_Text_02" {
  "Power-on is refused if the first tick cannot be paid. Running dry powers the set off."
 }
 Item "GBRS-FieldManual_Entry_Supply_Text_03" {
  "When the covering base flag changes hands: the station powers off, affiliation follows the occupier, and they may power it from their stockpile. Hardware mesh does not swap."
 }
 Item "GBRS-FieldManual_Entry_Supply_Text_04" {
  "Truck-built radars outside a base radius keep the builder faction. Intact stations are not shovel-dismantled. Destroyed wrecks: hold an entrenching tool and use Dismantle wreck."
 }
 Item "GBRS-FieldManual_Entry_Scope_Title" {
  "Ground Radar - Reading the Scope"
 }
 Item "GBRS-FieldManual_Entry_Scope_Text_01" {
  "Local search plots are anonymous skin returns - no secondary radar / IFF interrogation on the contact list."
 }
 Item "GBRS-FieldManual_Entry_Scope_Text_02" {
  "ANON = unknown air return. EMIT = radiating radar. SHELL = WLR projectile with launch / impact estimates (not which gun)."
 }
 Item "GBRS-FieldManual_Entry_Scope_Text_03" {
  "Multi-station datalink can color fused tracks friend / foe / neutral. That does not rename local ANON plots. Requires RDF 1.1.6+. RF numbers are Conflict-balanced, not real approach-handbook ranges."
 }
}
