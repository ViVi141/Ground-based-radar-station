//------------------------------------------------------------------------------------------------
//! Child-part damage relay for GBRS radar stations.
//!
//! Engine explosions (ExplosionDamageContainer / C4) deal damage only to the
//! DamageManager found ON the hit entity itself — they do NOT walk up the
//! parent hierarchy (unlike SCR_WeaponBlastComponent). A station's antenna /
//! generator children have their own colliders but no station-level damage
//! manager, so a C4 hit used to vanish without hurting the station.
//!
//! This component, mounted on each child part, IS a damage manager: the engine
//! delivers the blast to it, and OnDamage relays the context to the station
//! root's GBRS_RadarStationDamageManagerComponent (single HP pool).
//! The child HitZone itself is given a huge HP so it never actually dies — it
//! only exists as the engine's delivery target for the relay.
//!
//! Realism weighting: damaging the antenna array is far less lethal than
//! hitting the power/generator or the electronics. m_fRelayMultiplier scales
//! how much of the child's received damage reaches the station root pool:
//!   antenna    = 0.2  (antenna strikes mostly harm the array, not the radar)
//!   generator  = 1.0  (losing power is a direct, full threat to the station)
[ComponentEditorProps(category: "GameScripted/GBRS", description: "Relays blast damage from a station child part to the station root")]
class GBRS_RadarStationChildDamageComponentClass : SCR_DamageManagerComponentClass
{
	// Damage fraction relayed to the station root pool (0 = part is immune).
	[Attribute("1.0", UIWidgets.Slider, "Damage fraction relayed to the station root pool", "0 2 0.05")]
	float m_fRelayMultiplier;

	// Draw world-space damage debug shapes/text at the hit point (Workbench / dev builds).
	[Attribute("0", UIWidgets.CheckBox, "Draw damage debug shapes at the hit point", category: "Debug")]
	bool m_bDebugDraw;
}

class GBRS_RadarStationChildDamageComponent : SCR_DamageManagerComponent
{
	//------------------------------------------------------------------------------------------------
	override protected void OnDamage(notnull BaseDamageContext damageContext)
	{
		super.OnDamage(damageContext);

		if (IsDebugDraw())
			DebugLogAndDraw(damageContext);

		RelayToStationRoot(damageContext);
	}

	//------------------------------------------------------------------------------------------------
	//! Prints the raw hit and draws a marker at the hit point so we can see
	//! what damage actually lands on this part and what gets forwarded.
	protected void DebugLogAndDraw(notnull BaseDamageContext damageContext)
	{
		string partName = GetOwner().GetPrefabData().GetPrefabName().GetPath();
		string typeName = typename.EnumToString(EDamageType, damageContext.damageType);
		float raw = damageContext.damageValue;

		Print("[GBRS-DAMAGE] " + partName + " hit: type=" + typeName
			+ " raw=" + raw.ToString()
			+ " relayMult=" + GetRelayMultiplier().ToString(),
			LogLevel.WARNING);

		DrawHitMarker(damageContext, Color.RED, partName + "\n" + typeName + " " + raw.ToString());
	}

	//------------------------------------------------------------------------------------------------
	//! Spawns a sphere + one-shot text at the hit position. ONCE flags let the
	//! engine auto-clear them after a frame, so no manual lifetime management.
	protected void DrawHitMarker(notnull BaseDamageContext damageContext, int color, string label)
	{
		World world = GetOwner().GetWorld();
		if (!world)
			return;

		vector hitPos = damageContext.hitPosition;
		if (hitPos == vector.Zero)
			hitPos = GetOwner().GetOrigin();

		Shape.CreateSphere(
			color,
			ShapeFlags.ONCE | ShapeFlags.NOOUTLINE | ShapeFlags.TRANSP | ShapeFlags.NOZBUFFER,
			hitPos,
			0.12);

		DebugTextWorldSpace.Create(
			world,
			label,
			DebugTextFlags.ONCE | DebugTextFlags.CENTER | DebugTextFlags.FACE_CAMERA,
			hitPos[0], hitPos[1] + 0.3, hitPos[2],
			11,
			color,
			ARGB(200, 0, 0, 0));
	}

	//------------------------------------------------------------------------------------------------
	protected void RelayToStationRoot(notnull BaseDamageContext damageContext)
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		IEntity root = owner.GetRootParent();
		if (!root || root == owner)
			return;

		GBRS_RadarStationComponent station =
			GBRS_RadarStationComponent.Cast(root.FindComponent(GBRS_RadarStationComponent));
		if (!station)
			return;

		float multiplier = GetRelayMultiplier();
		if (multiplier <= 0.0)
			return;

		// The relay dampener only applies to blast/fragmentation - hitting the
		// antenna array with an explosion is cosmetic (20% of a C4 relays).
		// Kinetic and incendiary pass through at full value so the root
		// HitZone's own type multipliers + reduction decide: rifles (<=142
		// kinetic) still bounce off, while .50 (340) and HEIT (48 incendiary)
		// actually hurt the station. Dampening kinetic here too would double-
		// reduce them to zero (LAV-25/.50 "can't damage" bug).
		EDamageType damageType = damageContext.damageType;
		if (damageType != EDamageType.EXPLOSIVE && damageType != EDamageType.FRAGMENTATION)
			multiplier = 1.0;

		station.RelayDamageToStation(damageContext, multiplier);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsDebugDraw()
	{
		GBRS_RadarStationChildDamageComponentClass data =
			GBRS_RadarStationChildDamageComponentClass.Cast(GetComponentData(GetOwner()));
		if (!data)
			return false;

		return data.m_bDebugDraw;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetRelayMultiplier()
	{
		GBRS_RadarStationChildDamageComponentClass data =
			GBRS_RadarStationChildDamageComponentClass.Cast(GetComponentData(GetOwner()));
		if (!data)
			return 1.0;

		float mult = data.m_fRelayMultiplier;
		if (mult < 0.0)
			return 0.0;
		if (mult > 2.0)
			return 2.0;
		return mult;
	}
}
