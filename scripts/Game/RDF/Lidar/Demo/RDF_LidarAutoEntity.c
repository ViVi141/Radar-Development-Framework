[EntityEditorProps(category: "RDF/LiDAR", description: "Toggleable entity to control LiDAR auto-run demo", color: "0 200 0 255")]
class RDF_LidarAutoEntityClass : GenericEntityClass {}

class RDF_LidarAutoEntity : GenericEntity
{
    [Attribute("1", UIWidgets.CheckBox, "Enable/disable LiDAR auto-run demo on this entity")]
    bool m_bEnabled;

    protected bool m_bPrevEnabled;

    void RDF_LidarAutoEntity(IEntitySource src, IEntity parent)
    {
        SetFlags(EntityFlags.ACTIVE, true);
        SetEventMask(EntityEvent.FRAME);
        m_bPrevEnabled = !m_bEnabled;
        if (m_bEnabled)
            RDF_LidarAutoRunner.SetDemoEnabled(true);
    }

    override void EOnFrame(IEntity owner, float timeSlice)
    {
        if (m_bEnabled == m_bPrevEnabled)
            return;

        m_bPrevEnabled = m_bEnabled;
        if (m_bEnabled)
            RDF_LidarAutoRunner.SetDemoEnabled(true);
        else
            RDF_LidarAutoRunner.SetDemoEnabled(false);
    }

    void ~RDF_LidarAutoEntity()
    {
        if (m_bEnabled)
            RDF_LidarAutoRunner.SetDemoEnabled(false);
    }
}
