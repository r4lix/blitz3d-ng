// Blitz3D-TSS engine commands used by SCP:CB Ultimate Edition Reborn. Included at the end of blitz3d.cpp.

extern std::map<Entity*,void(*)(Entity*)> *entity_destructors;

BBLIB bb_int_t BBCALL bbEntityHidden( Entity *e ){
	if( !e ) return 1;	// TSS: a null entity counts as hidden
	debugEntity(e);
	return !e->visible();
}

BBLIB bb_float_t BBCALL bbEntityScaleX( Entity *e,bb_int_t global ){
	debugEntity(e);
	return global ? e->getWorldScale().x : e->getLocalScale().x;
}

BBLIB bb_float_t BBCALL bbEntityScaleY( Entity *e,bb_int_t global ){
	debugEntity(e);
	return global ? e->getWorldScale().y : e->getLocalScale().y;
}

BBLIB bb_float_t BBCALL bbEntityScaleZ( Entity *e,bb_int_t global ){
	debugEntity(e);
	return global ? e->getWorldScale().z : e->getLocalScale().z;
}

BBLIB bb_float_t BBCALL bbEntityDistanceSquared( Entity *a,Entity *b ){
	debugEntity(a);debugEntity(b);
	Vector d=a->getWorldPosition()-b->getWorldPosition();
	return d.x*d.x+d.y*d.y+d.z*d.z;
}

BBLIB bb_float_t BBCALL bbMeshX( MeshModel *m,bb_int_t max ){
	debugMesh(m);
	return max ? m->getBox().b.x : m->getBox().a.x;
}

BBLIB bb_float_t BBCALL bbMeshY( MeshModel *m,bb_int_t max ){
	debugMesh(m);
	return max ? m->getBox().b.y : m->getBox().a.y;
}

BBLIB bb_float_t BBCALL bbMeshZ( MeshModel *m,bb_int_t max ){
	debugMesh(m);
	return max ? m->getBox().b.z : m->getBox().a.z;
}

BBLIB bb_float_t BBCALL bbGetEntityAlpha( Model *m ){
	debugModel(m);
	return m->getBrush().getAlpha();
}

BBLIB bb_float_t BBCALL bbEntityColorR( Model *m ){
	debugModel(m);
	return m->getBrush().getColor().x/ctof;
}

BBLIB bb_float_t BBCALL bbEntityColorG( Model *m ){
	debugModel(m);
	return m->getBrush().getColor().y/ctof;
}

BBLIB bb_float_t BBCALL bbEntityColorB( Model *m ){
	debugModel(m);
	return m->getBrush().getColor().z/ctof;
}

BBLIB bb_int_t BBCALL bbGetEntityBlend( Model *m ){
	debugModel(m);
	return m->getBrush().getBlend();
}

BBLIB bb_float_t BBCALL bbGetCameraRangeNear( Camera *c ){
	debugCamera(c);
	return c->getFrustumNear();
}

BBLIB bb_float_t BBCALL bbGetCameraRangeFar( Camera *c ){
	debugCamera(c);
	return c->getFrustumFar();
}

// GPU instancing is an optimisation of the TSS renderer: copies are ordinary shared-geometry copies here
BBLIB void BBCALL bbEntityInstance( Entity *e,bb_int_t source ){}
BBLIB bb_int_t BBCALL bbGetInstance( Entity *e ){ return 0; }
BBLIB void BBCALL bbEntityCenter( Entity *e,bb_float_t x,bb_float_t y,bb_float_t z ){}

extern void *bbFuncPtrResolve( bb_int_t id );
static std::map<Entity*,bb_int_t> entity_dtor_ids;

static void entityDestructorTrampoline( Entity *e ){
	auto it=entity_dtor_ids.find( e );
	if( it==entity_dtor_ids.end() ) return;
	bb_int_t id=it->second;entity_dtor_ids.erase( it );
	typedef bb_int_t (*Fn)( bb_int_t );
	if( Fn f=(Fn)bbFuncPtrResolve( id ) ) f( (bb_int_t)(intptr_t)e );
}

BBLIB void BBCALL bbEntityDestructor( Entity *e,bb_int_t callback ){
	debugEntity(e);
	if( !entity_destructors ) entity_destructors=new std::map<Entity*,void(*)(Entity*)>;
	if( !callback ){
		entity_destructors->erase( e );entity_dtor_ids.erase( e );
		return;
	}
	(*entity_destructors)[e]=entityDestructorTrampoline;
	entity_dtor_ids[e]=callback;
}

BBLIB BBCanvas * BBCALL bbGetEntityTextureBuffer( Model *m,bb_int_t index ){
	debugModel(m);
	return 0;
}

// ---- physics ----
BBLIB void BBCALL bbEntityPhysics( Object *o,bb_int_t on ){
	debugObject(o);
	o->phys.on=on!=0;
	o->phys.lin=Vector();
	o->phys.awake=60;
}
BBLIB void BBCALL bbEntityMass( Object *o,bb_float_t v ){ debugObject(o);o->phys.mass=v; }
BBLIB void BBCALL bbEntityFriction( Object *o,bb_float_t v ){ debugObject(o);o->phys.friction=v; }
BBLIB void BBCALL bbEntityRestitution( Object *o,bb_float_t v ){ debugObject(o);o->phys.restitution=v; }
BBLIB void BBCALL bbEntityLinearDamping( Object *o,bb_float_t v ){ debugObject(o);o->phys.lin_damp=v; }
BBLIB void BBCALL bbEntityAngularDamping( Object *o,bb_float_t v ){ debugObject(o);o->phys.ang_damp=v; }
BBLIB void BBCALL bbEntityGravity( Object *o,bb_float_t v ){ debugObject(o);o->phys.gravity=v; }
BBLIB void BBCALL bbEntityKinematic( Object *o,bb_int_t on ){ debugObject(o);o->phys.kinematic=on!=0; }
BBLIB void BBCALL bbEntityActivate( Object *o,bb_int_t on ){ debugObject(o);o->phys.awake=on?60:0;o->phys.frozen=false; }
BBLIB void BBCALL bbEntitySleep( Object *o,bb_int_t on ){ debugObject(o);if( on ) o->phys.awake=0; }
BBLIB void BBCALL bbEntityFreeze( Object *o,bb_int_t on ){ debugObject(o);o->phys.frozen=on!=0;if( on ) o->phys.lin=Vector(); }
BBLIB bb_int_t BBCALL bbEntityIsActive( Object *o ){ debugObject(o);return o->phys.awake>0 && !o->phys.frozen; }
BBLIB bb_int_t BBCALL bbEntityIsFreezed( Object *o ){ debugObject(o);return o->phys.frozen; }
BBLIB void BBCALL bbEntityAngularFactor( Object *o,bb_float_t x,bb_float_t y,bb_float_t z ){ debugObject(o);o->phys.ang_factor=Vector( x,y,z ); }
BBLIB void BBCALL bbEntityLinearVelocity( Object *o,bb_float_t x,bb_float_t y,bb_float_t z ){
	debugObject(o);
	o->phys.lin=Vector( x,y,z );
	o->phys.awake=60;
}
BBLIB void BBCALL bbEntityTorque( Object *o,bb_float_t x,bb_float_t y,bb_float_t z ){ debugObject(o);o->phys.awake=60; }
BBLIB void BBCALL bbEntityClearForces( Object *o ){ debugObject(o);o->phys.lin=Vector(); }
BBLIB bb_float_t BBCALL bbCollisionImpulse( Object *o,bb_int_t index ){ debugObject(o);return o->phys.impulse; }
BBLIB bb_float_t BBCALL bbGetEntityVelX( Object *o ){ debugObject(o);return o->phys.lin.x; }
BBLIB bb_float_t BBCALL bbGetEntityVelY( Object *o ){ debugObject(o);return o->phys.lin.y; }
BBLIB bb_float_t BBCALL bbGetEntityVelZ( Object *o ){ debugObject(o);return o->phys.lin.z; }

BBLIB bb_int_t BBCALL bbTextureFlags( Texture *t ){ debugTexture(t);return t->getFlags(); }
BBLIB void BBCALL bbDrawBuffer( BBCanvas *c,bb_int_t x,bb_int_t y,bb_int_t w,bb_int_t h,bb_int_t blend ){}
BBLIB void BBCALL bbDrawBufferRect( BBCanvas *c,bb_int_t x,bb_int_t y,bb_int_t w,bb_int_t h,bb_int_t sx,bb_int_t sy,bb_int_t sw,bb_int_t sh,bb_int_t blend ){}

#include "tss_stubs.inc"
