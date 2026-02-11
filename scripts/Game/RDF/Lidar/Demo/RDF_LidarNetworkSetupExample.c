// Example: How to set up networked LiDAR for multiplayer games
// This script shows how to attach the network component to a player entity

[ComponentEditorProps(category: "GameScripted/RDF", description: "Example component that sets up networked LiDAR on a player")]
class RDF_LidarNetworkSetupExampleClass : ScriptComponentClass
{
}

class RDF_LidarNetworkSetupExample : ScriptComponent
{
	protected RDF_LidarNetworkComponent m_NetworkComponent;

	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		// Find or create the network component
		m_NetworkComponent = RDF_LidarNetworkComponent.Cast(owner.FindComponent(RDF_LidarNetworkComponent));
		if (!m_NetworkComponent)
		{
			// If not found, you would need to add it via entity prefab
			// For this example, we'll assume it's already attached
			Print("RDF_LidarNetworkSetupExample: RDF_LidarNetworkComponent not found on owner!", LogLevel.WARNING);
			return;
		}

		// Set the network component in the auto runner
		RDF_LidarAutoRunner.SetNetworkComponent(m_NetworkComponent);

		// Example: Set up a basic demo configuration
		RDF_LidarDemoConfig config = RDF_LidarDemoConfig.CreateDefault(128);
		config.m_RenderWorld = false; // Point cloud only mode for multiplayer
		RDF_LidarAutoRunner.SetDemoConfig(config);

		Print("RDF LiDAR Network Setup Complete", LogLevel.NORMAL);
	}

	// Example method to enable/disable LiDAR remotely
	void EnableLidarDemo(bool enable)
	{
		if (enable)
		{
			RDF_LidarAutoRunner.SetDemoEnabled(true);
			Print("LiDAR Demo Enabled via Network", LogLevel.NORMAL);
		}
		else
		{
			RDF_LidarAutoRunner.SetDemoEnabled(false);
			Print("LiDAR Demo Disabled via Network", LogLevel.NORMAL);
		}
	}

	// Example method to change scan strategy
	void SetScanStrategy(string strategyName)
	{
		RDF_LidarDemoConfig config;

		switch (strategyName)
		{
			case "hemisphere":
				config = RDF_LidarDemoConfig.CreateHemisphere(128);
				break;
			case "conical":
				config = RDF_LidarDemoConfig.CreateConical(30.0, 128);
				break;
			case "stratified":
				config = RDF_LidarDemoConfig.CreateStratified(128);
				break;
			default:
				config = RDF_LidarDemoConfig.CreateDefault(128);
				break;
		}

		if (config)
		{
			RDF_LidarAutoRunner.SetDemoConfig(config);
			Print("LiDAR Strategy changed to: " + strategyName, LogLevel.NORMAL);
		}
	}
}