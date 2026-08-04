//------------------------------------------------------------------------------------------------
//! Damage manager for GBRS placeable radar stations.
//! Uses a Default HitZone for HP; on DESTROYED the station powers down and stays as a wreck
//! (entity is not deleted — unlike SCR_DestructionDamageManagerComponent).
[ComponentEditorProps(category: "GameScripted/GBRS", description: "Radar station hit points and destroy shutdown")]
class GBRS_RadarStationDamageManagerComponentClass : SCR_DamageManagerComponentClass
{
	[Attribute("5000", UIWidgets.Slider, "Maximum hit points for the station Default HitZone", "100 100000 1", category: "GBRS Health")]
	float m_fMaxHealth;
}

class GBRS_RadarStationDamageManagerComponent : SCR_DamageManagerComponent
{
	protected bool m_bStationNotifiedDestroyed;

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
