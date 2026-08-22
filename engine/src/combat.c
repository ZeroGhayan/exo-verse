#include "exo/combat.h"

#define BLOCKSTUN 8
#define BLOCK_KB  40.0f

bool exo_aabb(const ExoRect *a, const ExoRect *b)
{
	return a->x < b->x + b->w && a->x + a->w > b->x &&
	       a->y < b->y + b->h && a->y + a->h > b->y;
}

void exo_fighter_init(ExoFighter *f, float x, float y, float w, float h)
{
	exo_body_init(&f->body, x, y, w, h);
	f->phase = EXO_PHASE_IDLE;
	f->timer = 0;
	f->hit_done = false;
	f->move = 0;
	f->hp_max = 100;
	f->hp = 100;
	f->type = EXO_NORMAL;
	f->type2 = EXO_NORMAL;
	f->crouched = false;
	f->cancel = false;
}

bool exo_fighter_attack(ExoFighter *f, const ExoMove *m)
{
	bool gatling;

	if (!m)
		return false;

	gatling = f->cancel &&
	          (f->phase == EXO_PHASE_ACTIVE ||
	           f->phase == EXO_PHASE_RECOVERY);

	if (!exo_fighter_can_act(f) && !gatling)
		return false;
	if (!f->body.grounded && !m->air_ok)
		return false;
	if (m->grab && gatling)
		return false;

	f->move = m;
	f->phase = EXO_PHASE_STARTUP;
	f->timer = m->startup;
	f->hit_done = false;
	f->cancel = false;
	return true;
}

bool exo_fighter_can_act(const ExoFighter *f)
{
	return f->phase == EXO_PHASE_IDLE;
}

bool exo_fighter_busy(const ExoFighter *f)
{
	return f->phase != EXO_PHASE_IDLE && f->phase != EXO_PHASE_BLOCK;
}

bool exo_fighter_blocking(const ExoFighter *f)
{
	return f->phase == EXO_PHASE_BLOCK || f->phase == EXO_PHASE_BLOCKSTUN;
}

void exo_fighter_guard(ExoFighter *f, bool hold)
{
	if (f->phase == EXO_PHASE_BLOCKSTUN)
		return;
	if (hold && f->phase == EXO_PHASE_IDLE && f->body.grounded) {
		f->phase = EXO_PHASE_BLOCK;
		f->timer = 0;
		f->move = 0;
		f->body.vx = 0.0f;
	} else if (!hold && f->phase == EXO_PHASE_BLOCK) {
		f->phase = EXO_PHASE_IDLE;
	}
}

void exo_fighter_tick(ExoFighter *f)
{
	if (f->move && f->move->air_ok && f->body.grounded &&
	    f->phase != EXO_PHASE_IDLE && f->phase != EXO_PHASE_HITSTUN) {
		f->phase = EXO_PHASE_IDLE;
		f->move = 0;
		f->timer = 0;
		return;
	}

	if (f->phase == EXO_PHASE_IDLE || f->phase == EXO_PHASE_BLOCK)
		return;
	if (f->timer > 0)
		f->timer--;
	if (f->timer > 0)
		return;

	if (f->phase == EXO_PHASE_GRAB || f->phase == EXO_PHASE_GRABBED) {
		if (f->timer > 0)
			f->timer--;
		return;
	}

	switch (f->phase) {
	case EXO_PHASE_STARTUP:
		f->phase = EXO_PHASE_ACTIVE;
		f->timer = f->move ? f->move->active : 1;
		break;
	case EXO_PHASE_ACTIVE:
		f->phase = EXO_PHASE_RECOVERY;
		f->timer = f->move ? f->move->recovery : 1;
		break;
	case EXO_PHASE_RECOVERY:
	case EXO_PHASE_HITSTUN:
		f->phase = EXO_PHASE_IDLE;
		f->move = 0;
		break;
	case EXO_PHASE_BLOCKSTUN:
		f->phase = EXO_PHASE_BLOCK;
		f->move = 0;
		break;
	default:
		f->phase = EXO_PHASE_IDLE;
		break;
	}
}

void exo_fighter_apply_grab(ExoFighter *victim, ExoFighter *attacker)
{
	if (!victim->body.grounded)
		return;

	attacker->phase = EXO_PHASE_GRAB;
	attacker->timer = 48;
	attacker->move = 0;
	attacker->hit_done = true;
	attacker->body.vx = 0.0f;
	attacker->crouched = false;

	victim->phase = EXO_PHASE_GRABBED;
	victim->timer = 48;
	victim->move = 0;
	victim->body.vx = 0.0f;
	victim->body.vy = 0.0f;
	victim->crouched = false;
}

bool exo_fighter_hitbox(const ExoFighter *f, ExoRect *out)
{
	const ExoMove *m = f->move;
	if (f->phase != EXO_PHASE_ACTIVE || !m)
		return false;
	if (f->body.facing >= 0)
		out->x = f->body.x + m->box_x;
	else
		out->x = f->body.x + f->body.w - m->box_x - m->box_w;
	out->y = f->body.y + m->box_y;
	out->w = m->box_w;
	out->h = m->box_h;
	return true;
}

ExoRect exo_fighter_hurt(const ExoFighter *f)
{
	ExoRect r;

	r.x = f->body.x;
	r.w = f->body.w;
	if (f->crouched) {
		r.h = 20.0f;
		r.y = f->body.y + f->body.h - r.h;
	} else {
		r.y = f->body.y;
		r.h = f->body.h;
	}
	return r;
}

void exo_fighter_crouch(ExoFighter *f, bool on)
{
	if (!exo_fighter_can_act(f) && f->phase != EXO_PHASE_BLOCK)
		return;
	f->crouched = on;
}

bool exo_fighter_in_front(const ExoFighter *att, const ExoFighter *vic)
{
	if (att->body.facing > 0)
		return vic->body.x >= att->body.x;
	return vic->body.x <= att->body.x;
}

void exo_fighter_apply_hit(ExoFighter *victim, const ExoFighter *attacker)
{
	const ExoMove *m = attacker->move;
	ExoElem at;
	uint16_t mul;
	int16_t dmg;
	float dir;

	if (!m)
		return;

	at = m->type;
	mul = exo_elem_mul2(at, victim->type, victim->type2);

	if (mul == 0) {
		victim->body.vx = 0.0f;
		victim->body.vy = 0.0f;
		victim->phase = EXO_PHASE_IDLE;
		victim->timer = 0;
		victim->move = 0;
		return;
	}

	dir = (float)attacker->body.facing;
	victim->body.vx = dir * m->kb_x;
	victim->body.vy = -m->kb_y;
	victim->body.grounded = false;
	victim->phase = EXO_PHASE_HITSTUN;
	victim->timer = m->hitstun;
	victim->move = 0;

	dmg = (int16_t)m->damage;
	if (at == attacker->type || at == attacker->type2)
		dmg = (int16_t)((int16_t)dmg * 150 / 100);
	dmg = (int16_t)(dmg * (int16_t)mul / 100);
	if (dmg < 1)
		dmg = 1;
	victim->hp = (int16_t)(victim->hp - dmg);
	if (victim->hp < 0)
		victim->hp = 0;
}

void exo_fighter_apply_block(ExoFighter *victim, const ExoFighter *attacker)
{
	float dir = (float)attacker->body.facing;
	victim->body.vx = dir * BLOCK_KB;
	victim->body.vy = 0.0f;
	victim->phase = EXO_PHASE_BLOCKSTUN;
	victim->timer = BLOCKSTUN;
	victim->move = 0;
}
