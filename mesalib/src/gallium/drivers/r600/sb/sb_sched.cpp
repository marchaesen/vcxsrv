/*
 * Copyright 2013 Vadim Girlin <vadimgirlin@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *      Vadim Girlin
 */

#define PSC_DEBUG 0

#if PSC_DEBUG
#define PSC_DUMP(a) do { a } while (0)
#else
#define PSC_DUMP(a)
#endif

#include "sb_bc.h"
#include "sb_shader.h"
#include "sb_pass.h"
#include "sb_sched.h"
#include "eg_sq.h" // V_SQ_CF_INDEX_NONE/0/1

namespace r600_sb {

rp_kcache_tracker::rp_kcache_tracker(shader &sh) : rp(), uc(),
		// FIXME: for now we'll use "two const pairs" limit for r600, same as
		// for other chips, otherwise additional check in alu_group_tracker is
		// required to make sure that all 4 consts in the group fit into 2
		// kcache sets
		sel_count(2) {}

bool rp_kcache_tracker::try_reserve(sel_chan r) {
	unsigned sel = kc_sel(r);

	for (unsigned i = 0; i < sel_count; ++i) {
		if (rp[i] == 0) {
			rp[i] = sel;
			++uc[i];
			return true;
		}
		if (rp[i] == sel) {
			++uc[i];
			return true;
		}
	}
	return false;
}

bool rp_kcache_tracker::try_reserve(node* n) {
	bool need_unreserve = false;
	vvec::iterator I(n->src.begin()), E(n->src.end());

	for (; I != E; ++I) {
		value *v = *I;
		if (v->is_kcache()) {
			if (!try_reserve(v->select))
				break;
			else
				need_unreserve = true;
		}
	}
	if (I == E)
		return true;

	if (need_unreserve && I != n->src.begin()) {
		do {
			--I;
			value *v =*I;
			if (v->is_kcache())
				unreserve(v->select);
		} while (I != n->src.begin());
	}
	return false;
}

inline
void rp_kcache_tracker::unreserve(node* n) {
	vvec::iterator I(n->src.begin()), E(n->src.end());
	for (; I != E; ++I) {
		value *v = *I;
		if (v->is_kcache())
			unreserve(v->select);
	}
}

void rp_kcache_tracker::unreserve(sel_chan r) {
	unsigned sel = kc_sel(r);

	for (unsigned i = 0; i < sel_count; ++i)
		if (rp[i] == sel) {
			if (--uc[i] == 0)
				rp[i] = 0;
			return;
		}
	assert(0);
	return;
}

bool literal_tracker::try_reserve(alu_node* n) {
	bool need_unreserve = false;

	vvec::iterator I(n->src.begin()), E(n->src.end());

	for (; I != E; ++I) {
		value *v = *I;
		if (v->is_literal()) {
			if (!try_reserve(v->literal_value))
				break;
			else
				need_unreserve = true;
		}
	}
	if (I == E)
		return true;

	if (need_unreserve && I != n->src.begin()) {
		do {
			--I;
			value *v =*I;
			if (v->is_literal())
				unreserve(v->literal_value);
		} while (I != n->src.begin());
	}
	return false;
}

void literal_tracker::unreserve(alu_node* n) {
	unsigned nsrc = n->bc.op_ptr->src_count, i;

	for (i = 0; i < nsrc; ++i) {
		value *v = n->src[i];
		if (v->is_literal())
			unreserve(v->literal_value);
	}
}

bool literal_tracker::try_reserve(literal l) {

	PSC_DUMP( sblog << "literal reserve " << l.u << "  " << l.f << "\n"; );

	for (unsigned i = 0; i < MAX_ALU_LITERALS; ++i) {
		if (lt[i] == 0) {
			lt[i] = l;
			++uc[i];
			PSC_DUMP( sblog << "  reserved new uc = " << uc[i] << "\n"; );
			return true;
		} else if (lt[i] == l) {
			++uc[i];
			PSC_DUMP( sblog << "  reserved uc = " << uc[i] << "\n"; );
			return true;
		}
	}
	PSC_DUMP( sblog << "  failed to reserve literal\n"; );
	return false;
}

void literal_tracker::unreserve(literal l) {

	PSC_DUMP( sblog << "literal unreserve " << l.u << "  " << l.f << "\n"; );

	for (unsigned i = 0; i < MAX_ALU_LITERALS; ++i) {
		if (lt[i] == l) {
			if (--uc[i] == 0)
				lt[i] = 0;
			return;
		}
	}
	assert(0);
	return;
}

static inline unsigned bs_cycle_vector(unsigned bs, unsigned src) {
	static const unsigned swz[VEC_NUM][3] = {
		{0, 1, 2}, {0, 2, 1}, {1, 2, 0}, {1, 0, 2}, {2, 0, 1}, {2, 1, 0}
	};
	assert(bs < VEC_NUM && src < 3);
	return swz[bs][src];
}

static inline unsigned bs_cycle_scalar(unsigned bs, unsigned src) {
	static const unsigned swz[SCL_NUM][3] = {
		{2, 1, 0}, {1, 2, 2}, {2, 1, 2}, {2, 2, 1}
	};

	if (bs >= SCL_NUM || src >= 3) {
		// this prevents gcc warning "array subscript is above array bounds"
		// AFAICS we should never hit this path
		abort();
	}
	return swz[bs][src];
}

static inline unsigned bs_cycle(bool trans, unsigned bs, unsigned src) {
	return trans ? bs_cycle_scalar(bs, src) : bs_cycle_vector(bs, src);
}

inline
bool rp_gpr_tracker::try_reserve(unsigned cycle, unsigned sel, unsigned chan) {
	++sel;
	if (rp[cycle][chan] == 0) {
		rp[cycle][chan] = sel;
		++uc[cycle][chan];
		return true;
	} else if (rp[cycle][chan] == sel) {
		++uc[cycle][chan];
		return true;
	}
	return false;
}

inline
void rp_gpr_tracker::unreserve(alu_node* n) {
	unsigned nsrc = n->bc.op_ptr->src_count, i;
	unsigned trans = n->bc.slot == SLOT_TRANS;
	unsigned bs = n->bc.bank_swizzle;
	unsigned opt = !trans
			&& n->bc.src[0].sel == n->bc.src[1].sel
			&& n->bc.src[0].chan == n->bc.src[1].chan;

	for (i = 0; i < nsrc; ++i) {
		value *v = n->src[i];
		if (v->is_readonly() || v->is_undef())
			continue;
		if (i == 1 && opt)
			continue;
		unsigned cycle = bs_cycle(trans, bs, i);
		unreserve(cycle, n->bc.src[i].sel, n->bc.src[i].chan);
	}
}

inline
void rp_gpr_tracker::unreserve(unsigned cycle, unsigned sel, unsigned chan) {
	++sel;
	assert(rp[cycle][chan] == sel && uc[cycle][chan]);
	if (--uc[cycle][chan] == 0)
		rp[cycle][chan] = 0;
}

inline
bool rp_gpr_tracker::try_reserve(alu_node* n) {
	unsigned nsrc = n->bc.op_ptr->src_count, i;
	unsigned trans = n->bc.slot == SLOT_TRANS;
	unsigned bs = n->bc.bank_swizzle;
	unsigned opt = !trans && nsrc >= 2 &&
			n->src[0] == n->src[1];

	bool need_unreserve = false;
	unsigned const_count = 0, min_gpr_cycle = 3;

	for (i = 0; i < nsrc; ++i) {
		value *v = n->src[i];
		if (v->is_readonly() || v->is_undef()) {
			const_count++;
			if (trans && const_count == 3)
				break;
		} else {
			if (i == 1 && opt)
				continue;

			unsigned cycle = bs_cycle(trans, bs, i);

			if (trans && cycle < min_gpr_cycle)
				min_gpr_cycle = cycle;

			if (const_count && cycle < const_count && trans)
				break;

			if (!try_reserve(cycle, n->bc.src[i].sel, n->bc.src[i].chan))
				break;
			else
				need_unreserve = true;
		}
	}

	if ((i == nsrc) && (min_gpr_cycle + 1 > const_count))
		return true;

	if (need_unreserve && i--) {
		do {
			value *v = n->src[i];
			if (!v->is_readonly() && !v->is_undef()) {
			if (i == 1 && opt)
				continue;
			unreserve(bs_cycle(trans, bs, i), n->bc.src[i].sel,
			          n->bc.src[i].chan);
			}
		} while (i--);
	}
	return false;
}

alu_group_tracker::alu_group_tracker(shader &sh)
	: sh(sh), kc(sh),
	  gpr(), lt(), slots(),
	  max_slots(sh.get_ctx().is_cayman() ? 4 : 5),
	  has_mova(), uses_ar(), has_predset(), has_kill(),
	  updates_exec_mask(), consumes_lds_oqa(), produces_lds_oqa(), chan_count(), interp_param(), next_id() {

	available_slots = sh.get_ctx().has_trans ? 0x1F : 0x0F;
}

inline
sel_chan alu_group_tracker::get_value_id(value* v) {
	unsigned &id = vmap[v];
	if (!id)
		id = ++next_id;
	return sel_chan(id, v->get_final_chan());
}

inline
void alu_group_tracker::assign_slot(unsigned slot, alu_node* n) {
	update_flags(n);
	slots[slot] = n;
	available_slots &= ~(1 << slot);

	unsigned param = n->interp_param();

	if (param) {
		assert(!interp_param || interp_param == param);
		interp_param = param;
	}
}


void alu_group_tracker::discard_all_slots(container_node &removed_nodes) {
	PSC_DUMP( sblog << "agt::discard_all_slots\n"; );
	discard_slots(~available_slots & ((1 << max_slots) - 1), removed_nodes);
}

void alu_group_tracker::discard_slots(unsigned slot_mask,
                                    container_node &removed_nodes) {

	PSC_DUMP(
		sblog << "discard_slots : packed_ops : "
			<< (unsigned)packed_ops.size() << "\n";
	);

	for (node_vec::iterator N, I = packed_ops.begin();
			I != packed_ops.end(); I = N) {
		N = I; ++N;

		alu_packed_node *n = static_cast<alu_packed_node*>(*I);
		unsigned pslots = n->get_slot_mask();

		PSC_DUMP(
			sblog << "discard_slots : packed slot_mask : " << pslots << "\n";
		);

		if (pslots & slot_mask) {

			PSC_DUMP(
				sblog << "discard_slots : discarding packed...\n";
			);

			removed_nodes.push_back(n);
			slot_mask &= ~pslots;
			N = packed_ops.erase(I);
			available_slots |= pslots;
			for (unsigned k = 0; k < max_slots; ++k) {
				if (pslots & (1 << k))
					slots[k] = NULL;
			}
		}
	}

	for (unsigned slot = 0; slot < max_slots; ++slot) {
		unsigned slot_bit = 1 << slot;

		if (slot_mask & slot_bit) {
			assert(!(available_slots & slot_bit));
			assert(slots[slot]);

			assert(!(slots[slot]->bc.slot_flags & AF_4SLOT));

			PSC_DUMP(
				sblog << "discarding slot " << slot << " : ";
				dump::dump_op(slots[slot]);
				sblog << "\n";
			);

			removed_nodes.push_back(slots[slot]);
			slots[slot] = NULL;
			available_slots |= slot_bit;
		}
	}

	alu_node *t = slots[4];
	if (t && (t->bc.slot_flags & AF_V)) {
		unsigned chan = t->bc.dst_chan;
		if (!slots[chan]) {
			PSC_DUMP(
				sblog << "moving ";
				dump::dump_op(t);
				sblog << " from trans slot to free slot " << chan << "\n";
			);

			slots[chan] = t;
			slots[4] = NULL;
			t->bc.slot = chan;
		}
	}

	reinit();
}

alu_group_node* alu_group_tracker::emit() {

	alu_group_node *g = sh.create_alu_group();

	lt.init_group_literals(g);

	for (unsigned i = 0; i < max_slots; ++i) {
		alu_node *n = slots[i];
		if (n) {
			g->push_back(n);
		}
	}
	return g;
}

bool alu_group_tracker::try_reserve(alu_node* n) {
	unsigned nsrc = n->bc.op_ptr->src_count;
	unsigned slot = n->bc.slot;
	bool trans = slot == 4;

	if (slots[slot])
		return false;

	unsigned flags = n->bc.op_ptr->flags;

	unsigned param = n->interp_param();

	if (param && interp_param && interp_param != param)
		return false;

	if ((flags & AF_KILL) && has_predset)
		return false;
	if ((flags & AF_ANY_PRED) && (has_kill || has_predset))
		return false;
	if ((flags & AF_MOVA) && (has_mova || uses_ar))
		return false;

	if (n->uses_ar() && has_mova)
		return false;

	if (consumes_lds_oqa)
		return false;
	if (n->consumes_lds_oq() && available_slots != (sh.get_ctx().has_trans ? 0x1F : 0x0F))
		return false;
	for (unsigned i = 0; i < nsrc; ++i) {

		unsigned last_id = next_id;

		value *v = n->src[i];
		if (!v->is_any_gpr() && !v->is_rel())
			continue;
		sel_chan vid = get_value_id(n->src[i]);

		if (vid > last_id && chan_count[vid.chan()] == 3) {
			return false;
		}

		n->bc.src[i].sel = vid.sel();
		n->bc.src[i].chan = vid.chan();
	}

	if (!lt.try_reserve(n))
		return false;

	if (!kc.try_reserve(n)) {
		lt.unreserve(n);
		return false;
	}

	unsigned fbs = n->forced_bank_swizzle();

	n->bc.bank_swizzle = 0;

	if (!trans && fbs)
		n->bc.bank_swizzle = VEC_210;

	if (gpr.try_reserve(n)) {
		assign_slot(slot, n);
		return true;
	}

	if (!fbs) {
		unsigned swz_num = trans ? SCL_NUM : VEC_NUM;
		for (unsigned bs = 0; bs < swz_num; ++bs) {
			n->bc.bank_swizzle = bs;
			if (gpr.try_reserve(n)) {
				assign_slot(slot, n);
				return true;
			}
		}
	}

	gpr.reset();

	slots[slot] = n;
	unsigned forced_swz_slots = 0;
	int first_slot = ~0, first_nf = ~0, last_slot = ~0;
	unsigned save_bs[5];

	for (unsigned i = 0; i < max_slots; ++i) {
		alu_node *a = slots[i];
		if (a) {
			if (first_slot == ~0)
				first_slot = i;
			last_slot = i;
			save_bs[i] = a->bc.bank_swizzle;
			if (a->forced_bank_swizzle()) {
				assert(i != SLOT_TRANS);
				forced_swz_slots |= (1 << i);
				a->bc.bank_swizzle = VEC_210;
				if (!gpr.try_reserve(a))
					assert(!"internal reservation error");
			} else {
				if (first_nf == ~0)
					first_nf = i;

				a->bc.bank_swizzle = 0;
			}
		}
	}

	if (first_nf == ~0) {
		assign_slot(slot, n);
		return true;
	}

	assert(first_slot != ~0 && last_slot != ~0);

	// silence "array subscript is above array bounds" with gcc 4.8
	if (last_slot >= 5)
		abort();

	int i = first_nf;
	alu_node *a = slots[i];
	bool backtrack = false;

	while (1) {

		PSC_DUMP(
			sblog << " bs: trying s" << i << " bs:" << a->bc.bank_swizzle
				<< " bt:" << backtrack << "\n";
		);

		if (!backtrack && gpr.try_reserve(a)) {
			PSC_DUMP(
				sblog << " bs: reserved s" << i << " bs:" << a->bc.bank_swizzle
					<< "\n";
			);

			while ((++i <= last_slot) && !slots[i]);
			if (i <= last_slot)
				a = slots[i];
			else
				break;
		} else {
			bool itrans = i == SLOT_TRANS;
			unsigned max_swz = itrans ? SCL_221 : VEC_210;

			if (a->bc.bank_swizzle < max_swz) {
				++a->bc.bank_swizzle;

				PSC_DUMP(
					sblog << " bs: inc s" << i << " bs:" << a->bc.bank_swizzle
						<< "\n";
				);

			} else {

				a->bc.bank_swizzle = 0;
				while ((--i >= first_nf) && !slots[i]);
				if (i < first_nf)
					break;
				a = slots[i];
				PSC_DUMP(
					sblog << " bs: unreserve s" << i << " bs:" << a->bc.bank_swizzle
						<< "\n";
				);
				gpr.unreserve(a);
				backtrack = true;

				continue;
			}
		}
		backtrack = false;
	}

	if (i == last_slot + 1) {
		assign_slot(slot, n);
		return true;
	}

	// reservation failed, restore previous state
	slots[slot] = NULL;
	gpr.reset();
	for (unsigned i = 0; i < max_slots; ++i) {
		alu_node *a = slots[i];
		if (a) {
			a->bc.bank_swizzle = save_bs[i];
			bool b = gpr.try_reserve(a);
			assert(b);
		}
	}

	kc.unreserve(n);
	lt.unreserve(n);
	return false;
}

bool alu_group_tracker::try_reserve(alu_packed_node* p) {
	bool need_unreserve = false;
	node_iterator I(p->begin()), E(p->end());

	for (; I != E; ++I) {
		alu_node *n = static_cast<alu_node*>(*I);
		if (!try_reserve(n))
			break;
		else
			need_unreserve = true;
	}

	if (I == E)  {
		packed_ops.push_back(p);
		return true;
	}

	if (need_unreserve) {
		while (--I != E) {
			alu_node *n = static_cast<alu_node*>(*I);
			slots[n->bc.slot] = NULL;
		}
		reinit();
	}
	return false;
}

void alu_group_tracker::reinit() {
	alu_node * s[5];
	memcpy(s, slots, sizeof(slots));

	reset(true);

	for (int i = max_slots - 1; i >= 0; --i) {
		if (s[i] && !try_reserve(s[i])) {
			sblog << "alu_group_tracker: reinit error on slot " << i <<  "\n";
			for (unsigned i = 0; i < max_slots; ++i) {
				sblog << "  slot " << i << " : ";
				if (s[i])
					dump::dump_op(s[i]);

				sblog << "\n";
			}
			assert(!"alu_group_tracker: reinit error");
		}
	}
}

void alu_group_tracker::reset(bool keep_packed) {
	kc.reset();
	gpr.reset();
	lt.reset();
	memset(slots, 0, sizeof(slots));
	vmap.clear();
	next_id = 0;
	produces_lds_oqa = 0;
	consumes_lds_oqa = 0;
	has_mova = false;
	uses_ar = false;
	has_predset = false;
	has_kill = false;
	updates_exec_mask = false;
	available_slots = sh.get_ctx().has_trans ? 0x1F : 0x0F;
	interp_param = 0;

	chan_count[0] = 0;
	chan_count[1] = 0;
	chan_count[2] = 0;
	chan_count[3] = 0;

	if (!keep_packed)
		packed_ops.clear();
}

void alu_group_tracker::update_flags(alu_node* n) {
	unsigned flags = n->bc.op_ptr->flags;
	has_kill |= (flags & AF_KILL);
	has_mova |= (flags & AF_MOVA);
	has_predset |= (flags & AF_ANY_PRED);
	uses_ar |= n->uses_ar();
	consumes_lds_oqa |= n->consumes_lds_oq();
	produces_lds_oqa |= n->produces_lds_oq();
	if (flags & AF_ANY_PRED) {
		if (n->dst[2] != NULL)
			updates_exec_mask = true;
	}
}

int post_scheduler::run() {
	return run_on(sh.root) ? 0 : 1;
}

bool post_scheduler::run_on(container_node* n) {
	int r = true;
	for (node_riterator I = n->rbegin(), E = n->rend(); I != E; ++I) {
		if (I->is_container()) {
			if (I->subtype == NST_BB) {
				bb_node* bb = static_cast<bb_node*>(*I);
				r = schedule_bb(bb);
			} else {
				r = run_on(static_cast<container_node*>(*I));
			}
			if (!r)
				break;
		}
	}
	return r;
}

void post_scheduler::init_uc_val(container_node *c, value *v) {
	node *d = v->any_def();
	if (d && d->parent == c)
		++ucm[d];
}

void post_scheduler::init_uc_vec(container_node *c, vvec &vv, bool src) {
	for (vvec::iterator I = vv.begin(), E = vv.end(); I != E; ++I) {
		value *v = *I;
		if (!v || v->is_readonly())
			continue;

		if (v->is_rel()) {
			init_uc_val(c, v->rel);
			init_uc_vec(c, v->muse, true);
		} if (src) {
			init_uc_val(c, v);
		}
	}
}

unsigned post_scheduler::init_ucm(container_node *c, node *n) {
	init_uc_vec(c, n->src, true);
	init_uc_vec(c, n->dst, false);

	uc_map::iterator F = ucm.find(n);
	return F == ucm.end() ? 0 : F->second;
}

bool post_scheduler::schedule_bb(bb_node* bb) {
	PSC_DUMP(
		sblog << "scheduling BB " << bb->id << "\n";
		if (!pending.empty())
			dump::dump_op_list(&pending);
	);

	assert(pending.empty());
	assert(bb_pending.empty());
	assert(ready.empty());

	bb_pending.append_from(bb);
	cur_bb = bb;

	node *n;

	while ((n = bb_pending.back())) {

		PSC_DUMP(
			sblog << "post_sched_bb ";
			dump::dump_op(n);
			sblog << "\n";
		);

		// May require emitting ALU ops to load index registers
		if (n->is_fetch_clause()) {
			n->remove();
			process_fetch(static_cast<container_node *>(n));
			continue;
		}

		if (n->is_alu_clause()) {
			n->remove();
			bool r = process_alu(static_cast<container_node*>(n));
			if (r)
				continue;
			return false;
		}

		n->remove();
		bb->push_front(n);
	}

	this->cur_bb = NULL;
	return true;
}

void post_scheduler::init_regmap() {

	regmap.clear();

	PSC_DUMP(
		sblog << "init_regmap: live: ";
		dump::dump_set(sh, live);
		sblog << "\n";
	);

	for (val_set::iterator I = live.begin(sh), E = live.end(sh); I != E; ++I) {
		value *v = *I;
		assert(v);
		if (!v->is_sgpr() || !v->is_prealloc())
			continue;

		sel_chan r = v->gpr;

		PSC_DUMP(
			sblog << "init_regmap:  " << r << " <= ";
			dump::dump_val(v);
			sblog << "\n";
		);

		assert(r);
		regmap[r] = v;
	}
}

static alu_node *create_set_idx(shader &sh, unsigned ar_idx) {
	alu_node *a = sh.create_alu();

	assert(ar_idx == V_SQ_CF_INDEX_0 || ar_idx == V_SQ_CF_INDEX_1);
	if (ar_idx == V_SQ_CF_INDEX_0)
		a->bc.set_op(ALU_OP0_SET_CF_IDX0);
	else
		a->bc.set_op(ALU_OP0_SET_CF_IDX1);
	a->bc.slot = SLOT_X;
	a->dst.resize(1); // Dummy needed for recolor

	PSC_DUMP(
		sblog << "created IDX load: ";
		dump::dump_op(a);
		sblog << "\n";
	);

	return a;
}

void post_scheduler::load_index_register(value *v, unsigned ar_idx)
{
	alu.reset();

	if (!sh.get_ctx().is_cayman()) {
		// Evergreen has to first load address register, then use CF_SET_IDX0/1
		alu_group_tracker &rt = alu.grp();
		alu_node *set_idx = create_set_idx(sh, ar_idx);
		if (!rt.try_reserve(set_idx)) {
			sblog << "can't emit SET_CF_IDX";
			dump::dump_op(set_idx);
			sblog << "\n";
		}
		process_group();

		if (!alu.check_clause_limits()) {
			// Can't happen since clause only contains MOVA/CF_SET_IDX0/1
		}
		alu.emit_group();
	}

	alu_group_tracker &rt = alu.grp();
	alu_node *a = alu.create_ar_load(v, ar_idx == V_SQ_CF_INDEX_1 ? SEL_Z : SEL_Y);

	if (!rt.try_reserve(a)) {
		sblog << "can't emit AR load : ";
		dump::dump_op(a);
		sblog << "\n";
	}

	process_group();

	if (!alu.check_clause_limits()) {
		// Can't happen since clause only contains MOVA/CF_SET_IDX0/1
	}

	alu.emit_group();
	alu.emit_clause(cur_bb);
}

void post_scheduler::process_fetch(container_node *c) {
	if (c->empty())
		return;

	for (node_iterator N, I = c->begin(), E = c->end(); I != E; I = N) {
		N = I;
		++N;

		node *n = *I;

		fetch_node *f = static_cast<fetch_node*>(n);

		PSC_DUMP(
			sblog << "process_tex ";
			dump::dump_op(n);
			sblog << "  ";
		);

		// TODO: If same values used can avoid reloading index register
		if (f->bc.sampler_index_mode != V_SQ_CF_INDEX_NONE ||
			f->bc.resource_index_mode != V_SQ_CF_INDEX_NONE) {
			unsigned index_mode = f->bc.sampler_index_mode != V_SQ_CF_INDEX_NONE ?
				f->bc.sampler_index_mode : f->bc.resource_index_mode;

			// Currently require prior opt passes to use one TEX per indexed op
			assert(f->parent->count() == 1);

			value *v = f->src.back(); // Last src is index offset
			assert(v);

			cur_bb->push_front(c);

			load_index_register(v, index_mode);
			f->src.pop_back(); // Don't need index value any more

			return;
		}
	}

	cur_bb->push_front(c);
}

bool post_scheduler::process_alu(container_node *c) {

	if (c->empty())
		return true;

	ucm.clear();
	alu.reset();

	live = c->live_after;

	init_globals(c->live_after, true);
	init_globals(c->live_before, true);

	init_regmap();

	update_local_interferences();

	for (node_riterator N, I = c->rbegin(), E = c->rend(); I != E; I = N) {
		N = I;
		++N;

		node *n = *I;
		unsigned uc = init_ucm(c, n);

		PSC_DUMP(
			sblog << "process_alu uc=" << uc << "  ";
			dump::dump_op(n);
			sblog << "  ";
		);

		if (uc) {
			n->remove();

			pending.push_back(n);
			PSC_DUMP( sblog << "pending\n"; );
		} else {
			release_op(n);
		}
	}

	return schedule_alu(c);
}

void post_scheduler::update_local_interferences() {

	PSC_DUMP(
		sblog << "update_local_interferences : ";
		dump::dump_set(sh, live);
		sblog << "\n";
	);


	for (val_set::iterator I = live.begin(sh), E = live.end(sh); I != E; ++I) {
		value *v = *I;
		if (v->is_prealloc())
			continue;

		v->interferences.add_set(live);
	}
}

void post_scheduler::update_live_src_vec(vvec &vv, val_set *born, bool src) {
	for (vvec::iterator I = vv.begin(), E = vv.end(); I != E; ++I) {
		value *v = *I;

		if (!v)
			continue;

		if (src && v->is_any_gpr()) {
			if (live.add_val(v)) {
				if (!v->is_prealloc()) {
					if (!cleared_interf.contains(v)) {
						PSC_DUMP(
							sblog << "clearing interferences for " << *v << "\n";
						);
						v->interferences.clear();
						cleared_interf.add_val(v);
					}
				}
				if (born)
					born->add_val(v);
			}
		} else if (v->is_rel()) {
			if (!v->rel->is_any_gpr())
				live.add_val(v->rel);
			update_live_src_vec(v->muse, born, true);
		}
	}
}

void post_scheduler::update_live_dst_vec(vvec &vv) {
	for (vvec::iterator I = vv.begin(), E = vv.end(); I != E; ++I) {
		value *v = *I;
		if (!v)
			continue;

		if (v->is_rel()) {
			update_live_dst_vec(v->mdef);
		} else if (v->is_any_gpr()) {
			if (!live.remove_val(v)) {
				PSC_DUMP(
						sblog << "failed to remove ";
				dump::dump_val(v);
				sblog << " from live : ";
				dump::dump_set(sh, live);
				sblog << "\n";
				);
			}
		}
	}
}

void post_scheduler::update_live(node *n, val_set *born) {
	update_live_dst_vec(n->dst);
	update_live_src_vec(n->src, born, true);
	update_live_src_vec(n->dst, born, false);
}

void post_scheduler::process_group() {
	alu_group_tracker &rt = alu.grp();

	val_set vals_born;

	recolor_locals();

	PSC_DUMP(
		sblog << "process_group: live_before : ";
		dump::dump_set(sh, live);
		sblog << "\n";
	);

	for (unsigned s = 0; s < ctx.num_slots; ++s) {
		alu_node *n = rt.slot(s);
		if (!n)
			continue;

		update_live(n, &vals_born);
	}

	PSC_DUMP(
		sblog << "process_group: live_after : ";
		dump::dump_set(sh, live);
		sblog << "\n";
	);

	update_local_interferences();

	for (unsigned i = 0; i < 5; ++i) {
		node *n = rt.slot(i);
		if (n && !n->is_mova()) {
			release_src_values(n);
		}
	}
}

void post_scheduler::init_globals(val_set &s, bool prealloc) {

	PSC_DUMP(
		sblog << "init_globals: ";
		dump::dump_set(sh, s);
		sblog << "\n";
	);

	for (val_set::iterator I = s.begin(sh), E = s.end(sh); I != E; ++I) {
		value *v = *I;
		if (v->is_sgpr() && !v->is_global()) {
			v->set_global();

			if (prealloc && v->is_fixed()) {
				v->set_prealloc();
			}
		}
	}
}

void post_scheduler::emit_index_registers() {
	for (unsigned i = 0; i < 2; i++) {
		if (alu.current_idx[i]) {
			regmap = prev_regmap;
			alu.discard_current_group();

			load_index_register(alu.current_idx[i], KC_INDEX_0 + i);
			alu.current_idx[i] = NULL;
		}
	}
}

void post_scheduler::emit_clause() {

	if (alu.current_ar) {
		emit_load_ar();
		process_group();
		if (!alu.check_clause_limits()) {
			// Can't happen since clause only contains MOVA/CF_SET_IDX0/1
		}
		alu.emit_group();
	}

	if (!alu.is_empty()) {
		alu.emit_clause(cur_bb);
	}

	emit_index_registers();
}

bool post_scheduler::schedule_alu(container_node *c) {

	assert(!ready.empty() || !ready_copies.empty());

	/* This number is rather arbitrary, important is that the scheduler has
	 * more than one try to create an instruction group
	 */
	int improving = 10;
	int last_pending = pending.count();
	while (improving > 0) {
		prev_regmap = regmap;
		if (!prepare_alu_group()) {

			int new_pending = pending.count();
			if ((new_pending < last_pending) || (last_pending == 0))
				improving = 10;
			else
				--improving;

			last_pending = new_pending;

			if (alu.current_idx[0] || alu.current_idx[1]) {
				regmap = prev_regmap;
				emit_clause();
				init_globals(live, false);

				continue;
			}

			if (alu.current_ar) {
				emit_load_ar();
				continue;
			} else
				break;
		}

		if (!alu.check_clause_limits()) {
			regmap = prev_regmap;
			emit_clause();
			init_globals(live, false);

			continue;
		}

		process_group();
		alu.emit_group();
	};

	if (!alu.is_empty()) {
		emit_clause();
	}

	if (!ready.empty()) {
		sblog << "##post_scheduler: unscheduled ready instructions :";
		dump::dump_op_list(&ready);
		assert(!"unscheduled ready instructions");
	}

	if (!pending.empty()) {
		sblog << "##post_scheduler: unscheduled pending instructions :";
		dump::dump_op_list(&pending);
		assert(!"unscheduled pending instructions");
	}
	return improving;
}

void post_scheduler::add_interferences(value *v, sb_bitset &rb, val_set &vs) {
	unsigned chan = v->gpr.chan();

	for (val_set::iterator I = vs.begin(sh), E = vs.end(sh);
			I != E; ++I) {
		value *vi = *I;
		sel_chan gpr = vi->get_final_gpr();

		if (vi->is_any_gpr() && gpr && vi != v &&
				(!v->chunk || v->chunk != vi->chunk) &&
				vi->is_fixed() && gpr.chan() == chan) {

			unsigned r = gpr.sel();

			PSC_DUMP(
				sblog << "\tadd_interferences: " << *vi << "\n";
			);

			if (rb.size() <= r)
				rb.resize(r + 32);
			rb.set(r);
		}
	}
}

void post_scheduler::set_color_local_val(value *v, sel_chan color) {
	v->gpr = color;

	PSC_DUMP(
		sblog << "     recolored: ";
		dump::dump_val(v);
		sblog << "\n";
	);
}

void post_scheduler::set_color_local(value *v, sel_chan color) {
	if (v->chunk) {
		vvec &vv = v->chunk->values;
		for (vvec::iterator I = vv.begin(), E = vv.end(); I != E; ++I) {
			value *v2 =*I;
			set_color_local_val(v2, color);
		}
		v->chunk->fix();
	} else {
		set_color_local_val(v, color);
		v->fix();
	}
}

bool post_scheduler::recolor_local(value *v) {

	sb_bitset rb;

	assert(v->is_sgpr());
	assert(!v->is_prealloc());
	assert(v->gpr);

	unsigned chan = v->gpr.chan();

	PSC_DUMP(
		sblog << "recolor_local: ";
		dump::dump_val(v);
		sblog << "   interferences: ";
		dump::dump_set(sh, v->interferences);
		sblog << "\n";
		if (v->chunk) {
			sblog << "     in chunk: ";
			coalescer::dump_chunk(v->chunk);
			sblog << "\n";
		}
	);

	if (v->chunk) {
		for (vvec::iterator I = v->chunk->values.begin(),
				E = v->chunk->values.end(); I != E; ++I) {
			value *v2 = *I;

			PSC_DUMP( sblog << "   add_interferences for " << *v2 << " :\n"; );

			add_interferences(v, rb, v2->interferences);
		}
	} else {
		add_interferences(v, rb, v->interferences);
	}

	PSC_DUMP(
		unsigned sz = rb.size();
		sblog << "registers bits: " << sz;
		for (unsigned r = 0; r < sz; ++r) {
			if ((r & 7) == 0)
				sblog << "\n  " << r << "   ";
			sblog << (rb.get(r) ? 1 : 0);
		}
	);

	bool no_temp_gprs = v->is_global();
	unsigned rs, re, pass = no_temp_gprs ? 1 : 0;

	while (pass < 2) {

		if (pass == 0) {
			rs = sh.first_temp_gpr();
			re = MAX_GPR;
		} else {
			rs = 0;
			re = sh.num_nontemp_gpr();
		}

		for (unsigned reg = rs; reg < re; ++reg) {
			if (reg >= rb.size() || !rb.get(reg)) {
				// color found
				set_color_local(v, sel_chan(reg, chan));
				return true;
			}
		}
		++pass;
	}

	assert(!"recolor_local failed");
	return true;
}

void post_scheduler::emit_load_ar() {

	regmap = prev_regmap;
	alu.discard_current_group();

	alu_group_tracker &rt = alu.grp();
	alu_node *a = alu.create_ar_load(alu.current_ar, SEL_X);

	if (!rt.try_reserve(a)) {
		sblog << "can't emit AR load : ";
		dump::dump_op(a);
		sblog << "\n";
	}

	alu.current_ar = 0;
}

bool post_scheduler::unmap_dst_val(value *d) {

	if (d == alu.current_ar) {
		emit_load_ar();
		return false;
	}

	if (d->is_prealloc()) {
		sel_chan gpr = d->get_final_gpr();
		rv_map::iterator F = regmap.find(gpr);
		value *c = NULL;
		if (F != regmap.end())
			c = F->second;

		if (c && c!=d && (!c->chunk || c->chunk != d->chunk)) {
			PSC_DUMP(
				sblog << "dst value conflict : ";
				dump::dump_val(d);
				sblog << "   regmap contains ";
				dump::dump_val(c);
				sblog << "\n";
			);
			assert(!"scheduler error");
			return false;
		} else if (c) {
			regmap.erase(F);
		}
	}
	return true;
}

bool post_scheduler::unmap_dst(alu_node *n) {
	value *d = n->dst.empty() ? NULL : n->dst[0];

	if (!d)
		return true;

	if (!d->is_rel()) {
		if (d && d->is_any_reg()) {

			if (d->is_AR()) {
				if (alu.current_ar != d) {
					sblog << "loading wrong ar value\n";
					assert(0);
				} else {
					alu.current_ar = NULL;
				}

			} else if (d->is_any_gpr()) {
				if (!unmap_dst_val(d))
					return false;
			}
		}
	} else {
		for (vvec::iterator I = d->mdef.begin(), E = d->mdef.end();
				I != E; ++I) {
			d = *I;
			if (!d)
				continue;

			assert(d->is_any_gpr());

			if (!unmap_dst_val(d))
				return false;
		}
	}
	return true;
}

bool post_scheduler::map_src_val(value *v) {

	if (!v->is_prealloc())
		return true;

	sel_chan gpr = v->get_final_gpr();
	rv_map::iterator F = regmap.find(gpr);
	value *c = NULL;
	if (F != regmap.end()) {
		c = F->second;
		if (!v->v_equal(c)) {
			PSC_DUMP(
				sblog << "can't map src value ";
				dump::dump_val(v);
				sblog << ", regmap contains ";
				dump::dump_val(c);
				sblog << "\n";
			);
			return false;
		}
	} else {
		regmap.insert(std::make_pair(gpr, v));
	}
	return true;
}

bool post_scheduler::map_src_vec(vvec &vv, bool src) {
	if (src) {
		// Handle possible UBO indexing
		bool ubo_indexing[2] = { false, false };
		for (vvec::iterator I = vv.begin(), E = vv.end(); I != E; ++I) {
			value *v = *I;
			if (!v)
				continue;

			if (v->is_kcache()) {
				unsigned index_mode = v->select.kcache_index_mode();
				if (index_mode == KC_INDEX_0 || index_mode == KC_INDEX_1) {
					ubo_indexing[index_mode - KC_INDEX_0] = true;
				}
			}
		}

		// idx values stored at end of src vec, see bc_parser::prepare_alu_group
		for (unsigned i = 2; i != 0; i--) {
			if (ubo_indexing[i-1]) {
				// TODO: skip adding value to kcache reservation somehow, causes
				// unnecessary group breaks and cache line locks
				value *v = vv.back();
				if (alu.current_idx[i-1] && alu.current_idx[i-1] != v) {
					PSC_DUMP(
						sblog << "IDX" << i-1 << " already set to " <<
						*alu.current_idx[i-1] << ", trying to set " << *v << "\n";
					);
					return false;
				}

				alu.current_idx[i-1] = v;
				PSC_DUMP(sblog << "IDX" << i-1 << " set to " << *v << "\n";);
			}
		}
	}

	for (vvec::iterator I = vv.begin(), E = vv.end(); I != E; ++I) {
		value *v = *I;
		if (!v)
			continue;

		if ((!v->is_any_gpr() || !v->is_fixed()) && !v->is_rel())
			continue;

		if (v->is_rel()) {
			value *rel = v->rel;
			assert(rel);

			if (!rel->is_const()) {
				if (!map_src_vec(v->muse, true))
					return false;

				if (rel != alu.current_ar) {
					if (alu.current_ar) {
						PSC_DUMP(
							sblog << "  current_AR is " << *alu.current_ar
								<< "  trying to use " << *rel << "\n";
						);
						return false;
					}

					alu.current_ar = rel;

					PSC_DUMP(
						sblog << "  new current_AR assigned: " << *alu.current_ar
							<< "\n";
					);
				}
			}

		} else if (src) {
			if (!map_src_val(v)) {
				return false;
			}
		}
	}
	return true;
}

bool post_scheduler::map_src(alu_node *n) {
	if (!map_src_vec(n->dst, false))
		return false;

	if (!map_src_vec(n->src, true))
		return false;

	return true;
}

void post_scheduler::dump_regmap() {

	sblog << "# REGMAP :\n";

	for(rv_map::iterator I = regmap.begin(), E = regmap.end(); I != E; ++I) {
		sblog << "  # " << I->first << " => " << *(I->second) << "\n";
	}

	if (alu.current_ar)
		sblog << "    current_AR: " << *alu.current_ar << "\n";
	if (alu.current_pr)
		sblog << "    current_PR: " << *alu.current_pr << "\n";
	if (alu.current_idx[0])
		sblog << "    current IDX0: " << *alu.current_idx[0] << "\n";
	if (alu.current_idx[1])
		sblog << "    current IDX1: " << *alu.current_idx[1] << "\n";
}

void post_scheduler::recolor_locals() {
	alu_group_tracker &rt = alu.grp();

	for (unsigned s = 0; s < ctx.num_slots; ++s) {
		alu_node *n = rt.slot(s);
		if (n) {
			value *d = n->dst[0];
			if (d && d->is_sgpr() && !d->is_prealloc()) {
				recolor_local(d);
			}
		}
	}
}

// returns true if there are interferences
bool post_scheduler::check_interferences() {

	alu_group_tracker &rt = alu.grp();

	unsigned interf_slots;

	bool discarded = false;

	PSC_DUMP(
			sblog << "check_interferences: before: \n";
	dump_regmap();
	);

	do {

		interf_slots = 0;

		for (unsigned s = 0; s < ctx.num_slots; ++s) {
			alu_node *n = rt.slot(s);
			if (n) {
				if (!unmap_dst(n)) {
					return true;
				}
			}
		}

		for (unsigned s = 0; s < ctx.num_slots; ++s) {
			alu_node *n = rt.slot(s);
			if (n) {
				if (!map_src(n)) {
					interf_slots |= (1 << s);
				}
			}
		}

		PSC_DUMP(
				for (unsigned i = 0; i < 5; ++i) {
					if (interf_slots & (1 << i)) {
						sblog << "!!!!!! interf slot: " << i << "  : ";
						dump::dump_op(rt.slot(i));
						sblog << "\n";
					}
				}
		);

		if (!interf_slots)
			break;

		PSC_DUMP( sblog << "ci: discarding slots " << interf_slots << "\n"; );

		rt.discard_slots(interf_slots, alu.conflict_nodes);
		regmap = prev_regmap;
		discarded = true;

	} while(1);

	PSC_DUMP(
		sblog << "check_interferences: after: \n";
		dump_regmap();
	);

	return discarded;
}

// add instruction(s) (alu_node or contents of alu_packed_node) to current group
// returns the number of added instructions on success
unsigned post_scheduler::try_add_instruction(node *n) {

	alu_group_tracker &rt = alu.grp();

	unsigned avail_slots = rt.avail_slots();

	// Cannot schedule in same clause as instructions using this index value
	if (!n->dst.empty() && n->dst[0] &&
		(n->dst[0] == alu.current_idx[0] || n->dst[0] == alu.current_idx[1])) {
		PSC_DUMP(sblog << "   CF_IDX source: " << *n->dst[0] << "\n";);
		return 0;
	}

	if (n->is_alu_packed()) {
		alu_packed_node *p = static_cast<alu_packed_node*>(n);
		unsigned slots = p->get_slot_mask();
		unsigned cnt = __builtin_popcount(slots);

		if ((slots & avail_slots) != slots) {
			PSC_DUMP( sblog << "   no slots \n"; );
			return 0;
		}

		p->update_packed_items(ctx);

		if (!rt.try_reserve(p)) {
			PSC_DUMP( sblog << "   reservation failed \n"; );
			return 0;
		}

		p->remove();
		return cnt;

	} else {
		alu_node *a = static_cast<alu_node*>(n);
		value *d = a->dst.empty() ? NULL : a->dst[0];

		if (d && d->is_special_reg()) {
			assert((a->bc.op_ptr->flags & AF_MOVA) || d->is_geometry_emit() || d->is_lds_oq() || d->is_lds_access() || d->is_scratch());
			d = NULL;
		}

		unsigned allowed_slots = ctx.alu_slots_mask(a->bc.op_ptr);
		unsigned slot;

		allowed_slots &= avail_slots;

		if (!allowed_slots)
			return 0;

		if (d) {
			slot = d->get_final_chan();
			a->bc.dst_chan = slot;
			allowed_slots &= (1 << slot) | 0x10;
		} else {
			if (a->bc.op_ptr->flags & AF_MOVA) {
				if (a->bc.slot_flags & AF_V)
					allowed_slots &= (1 << SLOT_X);
				else
					allowed_slots &= (1 << SLOT_TRANS);
			}
		}

		// FIXME workaround for some problems with MULADD in trans slot on r700,
		// (is it really needed on r600?)
		if ((a->bc.op == ALU_OP3_MULADD || a->bc.op == ALU_OP3_MULADD_IEEE) &&
				!ctx.is_egcm()) {
			allowed_slots &= 0x0F;
		}

		if (!allowed_slots) {
			PSC_DUMP( sblog << "   no suitable slots\n"; );
			return 0;
		}

		slot = __builtin_ctz(allowed_slots);
		a->bc.slot = slot;

		PSC_DUMP( sblog << "slot: " << slot << "\n"; );

		if (!rt.try_reserve(a)) {
			PSC_DUMP( sblog << "   reservation failed\n"; );
			return 0;
		}

		a->remove();
		return 1;
	}
}

bool post_scheduler::check_copy(node *n) {
	if (!n->is_copy_mov())
		return false;

	value *s = n->src[0];
	value *d = n->dst[0];

	if (!s->is_sgpr() || !d->is_sgpr())
		return false;

	if (!s->is_prealloc()) {
		recolor_local(s);

		if (!s->chunk || s->chunk != d->chunk)
			return false;
	}

	if (s->gpr == d->gpr) {

		PSC_DUMP(
			sblog << "check_copy: ";
			dump::dump_op(n);
			sblog << "\n";
		);

		rv_map::iterator F = regmap.find(d->gpr);
		bool gpr_free = (F == regmap.end());

		if (d->is_prealloc()) {
			if (gpr_free) {
				PSC_DUMP( sblog << "    copy not ready...\n";);
				return true;
			}

			value *rv = F->second;
			if (rv != d && (!rv->chunk || rv->chunk != d->chunk)) {
				PSC_DUMP( sblog << "    copy not ready(2)...\n";);
				return true;
			}

			unmap_dst(static_cast<alu_node*>(n));
		}

		if (s->is_prealloc() && !map_src_val(s))
			return true;

		update_live(n, NULL);

		release_src_values(n);
		n->remove();
		PSC_DUMP( sblog << "    copy coalesced...\n";);
		return true;
	}
	return false;
}

void post_scheduler::dump_group(alu_group_tracker &rt) {
	for (unsigned i = 0; i < 5; ++i) {
		node *n = rt.slot(i);
		if (n) {
			sblog << "slot " << i << " : ";
			dump::dump_op(n);
			sblog << "\n";
		}
	}
}

void post_scheduler::process_ready_copies() {

	node *last;

	do {
		last = ready_copies.back();

		for (node_iterator N, I = ready_copies.begin(), E = ready_copies.end();
				I != E; I = N) {
			N = I; ++N;

			node *n = *I;

			if (!check_copy(n)) {
				n->remove();
				ready.push_back(n);
			}
		}
	} while (last != ready_copies.back());

	update_local_interferences();
}


bool post_scheduler::prepare_alu_group() {

	alu_group_tracker &rt = alu.grp();

	unsigned i1 = 0;

	PSC_DUMP(
		sblog << "prepare_alu_group: starting...\n";
		dump_group(rt);
	);

	ready.append_from(&alu.conflict_nodes);

	// FIXME rework this loop

	do {

		process_ready_copies();

		++i1;

		for (node_iterator N, I = ready.begin(), E = ready.end(); I != E;
				I = N) {
			N = I; ++N;
			node *n = *I;

			PSC_DUMP(
				sblog << "p_a_g: ";
				dump::dump_op(n);
				sblog << "\n";
			);


			unsigned cnt = try_add_instruction(n);

			if (!cnt)
				continue;

			PSC_DUMP(
				sblog << "current group:\n";
				dump_group(rt);
			);

			if (rt.inst_count() == ctx.num_slots) {
				PSC_DUMP( sblog << " all slots used\n"; );
				break;
			}
		}

		if (!check_interferences())
			break;

		// don't try to add more instructions to the group with mova if this
		// can lead to breaking clause slot count limit - we don't want mova to
		// end up in the end of the new clause instead of beginning of the
		// current clause.
		if (rt.has_ar_load() && alu.total_slots() > 121)
			break;

		if (rt.inst_count() && i1 > 50)
			break;

		regmap = prev_regmap;

	} while (1);

	PSC_DUMP(
		sblog << " prepare_alu_group done, " << rt.inst_count()
	          << " slot(s) \n";

		sblog << "$$$$$$$$PAG i1=" << i1
				<< "  ready " << ready.count()
				<< "  pending " << pending.count()
				<< "  conflicting " << alu.conflict_nodes.count()
				<<"\n";

	);

	return rt.inst_count();
}

void post_scheduler::release_src_values(node* n) {
	release_src_vec(n->src, true);
	release_src_vec(n->dst, false);
}

void post_scheduler::release_op(node *n) {
	PSC_DUMP(
		sblog << "release_op ";
		dump::dump_op(n);
		sblog << "\n";
	);

	n->remove();

	if (n->is_copy_mov()) {
		ready_copies.push_back(n);
	} else if (n->is_mova() || n->is_pred_set()) {
		ready.push_front(n);
	} else {
		ready.push_back(n);
	}
}

void post_scheduler::release_src_val(value *v) {
	node *d = v->any_def();
	if (d) {
		if (!--ucm[d])
			release_op(d);
	}
}

void post_scheduler::release_src_vec(vvec& vv, bool src) {

	for (vvec::iterator I = vv.begin(), E = vv.end(); I != E; ++I) {
		value *v = *I;
		if (!v || v->is_readonly())
			continue;

		if (v->is_rel()) {
			release_src_val(v->rel);
			release_src_vec(v->muse, true);

		} else if (src) {
			release_src_val(v);
		}
	}
}

void literal_tracker::reset() {
	memset(lt, 0, sizeof(lt));
	memset(uc, 0, sizeof(uc));
}

void rp_gpr_tracker::reset() {
	memset(rp, 0, sizeof(rp));
	memset(uc, 0, sizeof(uc));
}

void rp_kcache_tracker::reset() {
	memset(rp, 0, sizeof(rp));
	memset(uc, 0, sizeof(uc));
}

void alu_kcache_tracker::reset() {
	memset(kc, 0, sizeof(kc));
	lines.clear();
}

void alu_clause_tracker::reset() {
	group = 0;
	slot_count = 0;
	outstanding_lds_oqa_reads = 0;
	grp0.reset();
	grp1.reset();
}

alu_clause_tracker::alu_clause_tracker(shader &sh)
	: sh(sh), kt(sh.get_ctx().hw_class), slot_count(),
	  grp0(sh), grp1(sh),
	  group(), clause(),
	  push_exec_mask(), outstanding_lds_oqa_reads(),
	  current_ar(), current_pr(), current_idx() {}

void alu_clause_tracker::emit_group() {

	assert(grp().inst_count());

	alu_group_node *g = grp().emit();

	if (grp().has_update_exec_mask()) {
		assert(!push_exec_mask);
		push_exec_mask = true;
	}

	assert(g);

	if (!clause) {
		clause = sh.create_clause(NST_ALU_CLAUSE);
	}

	clause->push_front(g);

	outstanding_lds_oqa_reads += grp().get_consumes_lds_oqa();
	outstanding_lds_oqa_reads -= grp().get_produces_lds_oqa();
	slot_count += grp().slot_count();

	new_group();

	PSC_DUMP( sblog << "   #### group emitted\n"; );
}

void alu_clause_tracker::emit_clause(container_node *c) {
	assert(clause);

	kt.init_clause(clause->bc);

	assert(!outstanding_lds_oqa_reads);
	assert(!current_ar);
	assert(!current_pr);

	if (push_exec_mask)
		clause->bc.set_op(CF_OP_ALU_PUSH_BEFORE);

	c->push_front(clause);

	clause = NULL;
	push_exec_mask = false;
	slot_count = 0;
	kt.reset();

	PSC_DUMP( sblog << "######### ALU clause emitted\n"; );
}

bool alu_clause_tracker::check_clause_limits() {

	alu_group_tracker &gt = grp();

	unsigned slots = gt.slot_count();

	// reserving slots to load AR and PR values
	unsigned reserve_slots = (current_ar ? 1 : 0) + (current_pr ? 1 : 0);
	// ...and index registers
	reserve_slots += (current_idx[0] != NULL) + (current_idx[1] != NULL);

	if (gt.get_consumes_lds_oqa() && !outstanding_lds_oqa_reads)
		reserve_slots += 60;

	if (slot_count + slots > MAX_ALU_SLOTS - reserve_slots)
		return false;

	if (!kt.try_reserve(gt))
		return false;

	return true;
}

void alu_clause_tracker::new_group() {
	group = !group;
	grp().reset();
}

bool alu_clause_tracker::is_empty() {
	return clause == NULL;
}

void literal_tracker::init_group_literals(alu_group_node* g) {

	g->literals.clear();
	for (unsigned i = 0; i < 4; ++i) {
		if (!lt[i])
			break;

		g->literals.push_back(lt[i]);

		PSC_DUMP(
			sblog << "literal emitted: " << lt[i].f;
			sblog.print_zw_hex(lt[i].u, 8);
			sblog << "   " << lt[i].i << "\n";
		);
	}
}

bool alu_kcache_tracker::try_reserve(alu_group_tracker& gt) {
	rp_kcache_tracker &kt = gt.kcache();

	if (!kt.num_sels())
		return true;

	sb_set<unsigned> group_lines;

	unsigned nl = kt.get_lines(group_lines);
	assert(nl);

	sb_set<unsigned> clause_lines(lines);
	lines.add_set(group_lines);

	if (clause_lines.size() == lines.size())
		return true;

	if (update_kc())
		return true;

	lines = clause_lines;

	return false;
}

unsigned rp_kcache_tracker::get_lines(kc_lines& lines) {
	unsigned cnt = 0;

	for (unsigned i = 0; i < sel_count; ++i) {
		unsigned line = rp[i] & 0x1fffffffu;
		unsigned index_mode = rp[i] >> 29;

		if (!line)
			return cnt;

		--line;
		line = (sel_count == 2) ? line >> 5 : line >> 6;
		line |= index_mode << 29;

		if (lines.insert(line).second)
			++cnt;
	}
	return cnt;
}

bool alu_kcache_tracker::update_kc() {
	unsigned c = 0;

	bc_kcache old_kc[4];
	memcpy(old_kc, kc, sizeof(kc));

	for (kc_lines::iterator I = lines.begin(), E = lines.end(); I != E; ++I) {
		unsigned index_mode = *I >> 29;
		unsigned line = *I & 0x1fffffffu;
		unsigned bank = line >> 8;

		assert(index_mode <= KC_INDEX_INVALID);
		line &= 0xFF;

		if (c && (bank == kc[c-1].bank) && (kc[c-1].addr + 1 == line) &&
			kc[c-1].index_mode == index_mode)
		{
			kc[c-1].mode = KC_LOCK_2;
		} else {
			if (c == max_kcs) {
				memcpy(kc, old_kc, sizeof(kc));
				return false;
			}

			kc[c].mode = KC_LOCK_1;

			kc[c].bank = bank;
			kc[c].addr = line;
			kc[c].index_mode = index_mode;
			++c;
		}
	}
	return true;
}

alu_node* alu_clause_tracker::create_ar_load(value *v, chan_select ar_channel) {
	alu_node *a = sh.create_alu();

	if (sh.get_ctx().uses_mova_gpr) {
		a->bc.set_op(ALU_OP1_MOVA_GPR_INT);
		a->bc.slot = SLOT_TRANS;
	} else {
		a->bc.set_op(ALU_OP1_MOVA_INT);
		a->bc.slot = SLOT_X;
	}
	a->bc.dst_chan = ar_channel;
	if (ar_channel != SEL_X && sh.get_ctx().is_cayman()) {
		a->bc.dst_gpr = ar_channel == SEL_Y ? CM_V_SQ_MOVA_DST_CF_IDX0 : CM_V_SQ_MOVA_DST_CF_IDX1;
	}

	a->dst.resize(1);
	a->src.push_back(v);

	PSC_DUMP(
		sblog << "created AR load: ";
		dump::dump_op(a);
		sblog << "\n";
	);

	return a;
}

void alu_clause_tracker::discard_current_group() {
	PSC_DUMP( sblog << "act::discard_current_group\n"; );
	grp().discard_all_slots(conflict_nodes);
}

void rp_gpr_tracker::dump() {
	sblog << "=== gpr_tracker dump:\n";
	for (int c = 0; c < 3; ++c) {
		sblog << "cycle " << c << "      ";
		for (int h = 0; h < 4; ++h) {
			sblog << rp[c][h] << ":" << uc[c][h] << "   ";
		}
		sblog << "\n";
	}
}

} // namespace r600_sb
