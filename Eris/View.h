#ifndef ERIS_VIEW_H
#define ERIS_VIEW_H

#include <Eris/Types.h>
#include <Atlas/Objects/ObjectsFwd.h>
#include <sigc++/object.h>

namespace Eris
{

class Avatar;
class Entity;
class Connection;

/** View encapsulates the set of entities currently visible to an Avatar,
 as well as those that have recently been seen. It recieves visibility-affecting
 ops from the IGRouter, and uses them to update it's state and emit signals.
 It makes it's best effort to be correct event when edge cases happen (eg,
 entities being destroyed or disappearing very soon after appearance, and
 before the initial SIGHT is recived)
 */
class View : public SigC::Object
{
public:
    View(Avatar* av);
    
    /** test if the specified entity is in this View */
    bool isEntityVisible(Entity* ent) const;

    /** test if the entity with the specified ID is in this View */
    bool isEntityVisible(const std::string& eid) const;
    
    Entity* getEntity(const std::string& eid) const;
    
    Entity* getTopLevel() const;
    
    SigC::Signal1<void, Entity*> EntityCreated;
    SigC::Signal1<void, Entity*> EntityDeleted;
    SigC::Signal1<void, Entity*> Apperance;
    SigC::Signal1<void, Entity*> Disappearance;
    
protected:    
    // the router passes various relevant things to us directly
    friend class IGRouter;
    
    void appear(const std::string& eid, float stamp);
    void disappear(const std::string& eid);
    void initialSight(const Atlas::Objects::Entity::GameEntity& ge);
    void create(const Atlas::Objects::Entity::GameEntity& ge);
    void deleteEntity(const std::string& eid);
    
    /** retrieve the specified entity if it exists in the view at all, or
    return NULL otherwise */
    Entity* getExistingEntity(comnst std::string& id) const;
    
    /// test if the specified entity ID is pending initial sight on the View
    bool isPending(const std::string& const);
    
private:
    Connection* getConnection() const;
    void getEntityFromServer(const std::string& eid);
    
    void cancelPendingSight(const std::string& eid);
    
    Avatar* m_owner;
    IdEntityMap m_visible,
        m_invisible;
    Entity* m_topLevel; ///< the top-level visible entity for this view
    
    SigC::Signal1<void, Entity*> InitialSightEntity;
    
    typedef std::set<std::string> StringSet;
    StringSet m_pendingEntitySet;
    
    /** to correctly handle deletes/disappears soon after looks/appears, we
    record entities in this set, and check it in the initialSight code. */
    StringSet m_cancelledSightSet;
};

} // of namespace Eris

#endif // of ERIS_VIEW_H