// Network helper utilities for LiDAR.
class RDF_LidarNetworkUtils
{
	//------------------------------------------------------------------------------------------------
	//! Find the network API on an entity or its parents.
	static RDF_LidarNetworkAPI FindNetworkAPI(IEntity entity)
	{
		IEntity current = entity;
		int guard = 0;
		while (current && guard < 8)
		{
			RDF_LidarNetworkAPI api = RDF_LidarNetworkAPI.Cast(current.FindComponent(RDF_LidarNetworkAPI));
			if (api)
				return api;
			current = current.GetParent();
			guard++;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Bind AutoRunner to the local subject's network API, if present.
	//! \return true if bound
	static bool BindAutoRunnerToLocalSubject(bool preferVehicle = true)
	{
		IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(preferVehicle);
		if (!subject)
			return false;

		RDF_LidarNetworkAPI api = FindNetworkAPI(subject);
		if (!api)
			return false;

		RDF_LidarAutoRunner.SetNetworkAPI(api);
		return true;
	}
}
