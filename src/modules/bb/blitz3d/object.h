
#ifndef OBJECT_H
#define OBJECT_H

#include "../stdutil/stdutil.h"
#include <vector>

#include "entity.h"
#include "animator.h"
#include "collision.h"

#include <bb/audio/driver.h>

struct ObjCollision{
	Object *with;
	Vector coords;
	Collision collision;
};

class Object : public Entity{
public:
	typedef std::vector<const ObjCollision*> Collisions;

	Object();
	Object( const Object &object );
	~Object();

	//Entity interface
	Object *getObject(){ return this; }
	Entity *clone(){ return d_new Object( *this ); }

	//deep object copy!
	Object *copy();

	//called by user
	void reset();
	void setCollisionType( int type );
	void setCollisionRadii( const Vector &radii );
	void setCollisionBox( const Box &box );
	void setOrder( int n ){ order=n; }
	void setPickGeometry( int n ){ pick_geom=n; }
	void setObscurer( bool t ){ obscurer=t; }
	void setAnimation( const Animation &t ){ anim=t; }
	void setAnimator( Animator *t );

	BBChannel *emitSound( BBSound *sound );

	//overridables!
	virtual bool collide( const Line &line,float radius,::Collision *curr_coll,const Transform &t ){ return false; }
	virtual void capture();
	virtual void animate( float e );
	virtual bool beginRender( float tween );
	virtual void endRender();

	//for use by world
	void beginUpdate( float elapsed );
	void addCollision( const ObjCollision *c );
	void endUpdate();

	//accessors
	int getCollisionType()const;
	const Vector &getCollisionRadii()const;
	const Box &getCollisionBox()const;
	int getOrder()const{ return order; }
	const Vector &getVelocity()const;
	const Collisions &getCollisions()const;
	const Transform &getRenderTform()const;
	const Transform &getPrevWorldTform()const;
	int getPickGeometry()const{ return pick_geom; }
	int getObscurer()const{ return obscurer; }
	Animation getAnimation()const{ return anim; }
	Animator *getAnimator()const{ return animator; }
	Object *getLastCopy()const{ return last_copy; }

	// rigid-body state (Blitz3D-TSS EntityPhysics & co), integrated by World::update on top of the Blitz collision pass
	struct Phys{
		bool on=false,kinematic=false,frozen=false;
		float gravity=1,mass=1,friction=0.5f,restitution=0,lin_damp=0,ang_damp=0;
		int awake=0;			// physics steps left before the body counts as asleep
		Vector lin,ang;			// linear (units/s) and angular velocity
		Vector ang_factor=Vector(1,1,1);
		Vector start;			// world position before the step
		float impulse=0;		// impact of the last step
	} phys;

private:
	int coll_type;
	int order;
	Vector coll_radii;
	Collisions colls;
	bool captured;
	Box coll_box;
	int pick_geom;
	bool obscurer;
	float elapsed;
	Vector velocity;
	std::vector<BBChannel*> channels;
	Vector capt_pos,capt_scl;
	Quat capt_rot;
	mutable Object *last_copy;

	Transform prev_tform;
	Transform captured_tform,tween_tform;
	mutable Transform render_tform;
	mutable bool render_tform_valid;

	Animation anim;
	Animator *animator;

	void updateSounds();
};

#endif
