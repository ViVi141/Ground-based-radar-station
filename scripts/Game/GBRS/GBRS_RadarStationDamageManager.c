//------------------------------------------------------------------------------------------------
//! Damage manager for GBRS placeable radar stations.
//! Uses a Default HitZone for HP; on DESTROYED the station powers down and stays as a wreck
//! (entity is not deleted — unlike SCR_DestructionDamageManagerComponent).
[ComponentEditorProps(category: "GameScripted/GBRS", description: "Radar station hit points and destroy shutdown")]
class GBRS_RadarStationDamageManagerComponentClass : SCR_DamageManagerComponentClass
{
	[Attribute("5000", UIWidgets.Slider, "Maximum hit points for the station Default HitZone", "100 100000 1", category: "GBRS Health")]
	float m_fMaxHealth;

	// Draw world-space damage debug for direct hits on the station root body.
	[Attribute("1", UIWidgets.CheckBox, "Draw damage debug text/spheres for root hits", category: "Debug")]
	bool m_bDebugDraw;
}

class GBRS_RadarStationDamageManagerComponent : SCR_DamageManagerComponent
{
	protected bool m_bStationNotifiedDestroyed;

	//------------------------------------------------------------------------------------------------
	override protected void OnDamage(notnull BaseDamageContext damageContext)
	{
		super.OnDamage(damageContext);

		DebugLogAndDraw(damageContext);
	}

	//------------------------------------------------------------------------------------------------
	//! Prints and draws direct damage to the station root body (hits that do
	//! NOT come through the child relay: base/shack/mast shots).
	//! Always on: hits are sparse events so this cannot spam the log.
	protected void DebugLogAndDraw(notnull BaseDamageContext damageContext)
	{
		string typeName = typename.EnumToString(EDamageType, damageContext.damageType);
		float raw = damageContext.damageValue;

		Print("[GBRS-DAMAGE] station root hit: type=" + typeName
			+ " raw=" + raw.ToString()
			+ " hp=" + GetStationHealth().ToString() + "/" + GetStationMaxHealth().ToString(),
			LogLevel.WARNING);

		World world = GetOwner().GetWorld();
		if (!world)
			return;

		vector hitPos = damageContext.hitPosition;
		if (hitPos == vector.Zero)
			hitPos = GetOwner().GetOrigin();

		// ONCE flags: engine auto-clears the shape/text after a frame.
		Shape.CreateSphere(
			Color.ORANGE,
			ShapeFlags.ONCE | ShapeFlags.NOOUTLINE | ShapeFlags.TRANSP | ShapeFlags.NOZBUFFER,
			hitPos,
			0.15);

		DebugTextWorldSpace.Create(
			world,
			"ROOT " + typeName + " " + raw.ToString() + "\nhp " + GetStationHealth().ToString(),
			DebugTextFlags.ONCE | DebugTextFlags.CENTER | DebugTextFlags.FACE_CAMERA,
			hitPos[0], hitPos[1] + 0.4, hitPos[2],
			11,
			Color.ORANGE,
			ARGB(200, 0, 0, 0));
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		GBRS_RadarStationDamageManagerComponentClass data =
			GBRS_RadarStationDamageManagerComponentClass.Cast(GetComponentData(owner));
		if (!data)
			return;

		HitZone defaultZone = GetDefaultHitZone();
		if (!defaultZone)
			return;

		float maxHp = data.m_fMaxHealth;
		if (maxHp < 1.0)
			maxHp = 1.0;

		defaultZone.SetMaxHealth(maxHp);
		defaultZone.SetHealth(maxHp);
	}

	//------------------------------------------------------------------------------------------------
	override event protected void OnDamageStateChanged(
		EDamageState newState,
		EDamageState previousDamageState,
		bool isJIP)
	{
		super.OnDamageStateChanged(newState, previousDamageState, isJIP);

		if (newState != EDamageState.DESTROYED)
			return;

		if (m_bStationNotifiedDestroyed)
			return;

		m_bStationNotifiedDestroyed = true;

		IEntity owner = GetOwner();
		if (!owner)
			return;

		GBRS_RadarStationComponent station =
			GBRS_RadarStationComponent.Cast(owner.FindComponent(GBRS_RadarStationComponent));
		if (!station)
			return;

		station.OnStationDestroyed();
	}

	//------------------------------------------------------------------------------------------------
	float GetStationHealth()
	{
		return GetHealth();
	}

	//------------------------------------------------------------------------------------------------
	float GetStationMaxHealth()
	{
		return GetMaxHealth();
	}

	//------------------------------------------------------------------------------------------------
	float GetStationHealthScaled()
	{
		float maxHp = GetMaxHealth();
		if (maxHp <= 0.0)
			return 0.0;
		return GetHealth() / maxHp;
	}

	//------------------------------------------------------------------------------------------------
	bool IsStationDestroyed()
	{
		return GetState() == EDamageState.DESTROYED;
	}
}
