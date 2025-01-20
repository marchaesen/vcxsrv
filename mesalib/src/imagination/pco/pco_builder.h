/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_BUILDER_H
#define PCO_BUILDER_H

/**
 * \\file pco_builder.h
 *
 * \\brief PCO builder header.
 */

#include "pco.h"
#include "pco_internal.h"
#include "util/list.h"
#include "util/macros.h"

#include <stdbool.h>

/** Cursor pointer. */
enum pco_cursor_option {
   PCO_CURSOR_BEFORE_CF_NODE,
   PCO_CURSOR_AFTER_CF_NODE,

   PCO_CURSOR_BEFORE_INSTR,
   PCO_CURSOR_AFTER_INSTR,

   PCO_CURSOR_BEFORE_IGRP,
   PCO_CURSOR_AFTER_IGRP,
};

/** Cursor for PCO instructions/groups and basic blocks. */
typedef struct pco_cursor {
   enum pco_cursor_option option; /** Cursor pointer option. */
   union {
      pco_cf_node *cf_node;
      pco_instr *instr;
      pco_igrp *igrp;
   };
} pco_cursor;

/** PCO builder context. */
typedef struct pco_builder {
   pco_func *func; /** Target function. */
   pco_cursor cursor; /** Current position in the function. */
} pco_builder;

/* Cursor position setters. */
/**
 * \brief Returns a cursor set to before a cf node.
 *
 * \param[in] cf_node The cf node.
 * \return The cursor.
 */
static inline pco_cursor pco_cursor_before_cf_node(pco_cf_node *cf_node)
{
   return (pco_cursor){
      .option = PCO_CURSOR_BEFORE_CF_NODE,
      .cf_node = cf_node,
   };
}

/**
 * \brief Returns a cursor set to after a cf node.
 *
 * \param[in] cf_node The cf node.
 * \return The cursor.
 */
static inline pco_cursor pco_cursor_after_cf_node(pco_cf_node *cf_node)
{
   return (pco_cursor){
      .option = PCO_CURSOR_AFTER_CF_NODE,
      .cf_node = cf_node,
   };
}

/**
 * \brief Returns a cursor set to before a block.
 *
 * \param[in] block The block.
 * \return The cursor.
 */
static inline pco_cursor pco_cursor_before_block(pco_block *block)
{
   return pco_cursor_before_cf_node(&block->cf_node);
}

/**
 * \brief Returns a cursor set to after a block.
 *
 * \param[in] block The block.
 * \return The cursor.
 */
static inline pco_cursor pco_cursor_after_block(pco_block *block)
{
   return pco_cursor_after_cf_node(&block->cf_node);
}

/**
 * \brief Returns a cursor set to before an instruction.
 *
 * \param[in] instr The instruction.
 * \return The cursor.
 */
static inline pco_cursor pco_cursor_before_instr(pco_instr *instr)
{
   return (pco_cursor){
      .option = PCO_CURSOR_BEFORE_INSTR,
      .instr = instr,
   };
}

/**
 * \brief Returns a cursor set to after an instruction.
 *
 * \param[in] instr The instruction.
 * \return The cursor.
 */
static inline pco_cursor pco_cursor_after_instr(pco_instr *instr)
{
   return (pco_cursor){
      .option = PCO_CURSOR_AFTER_INSTR,
      .instr = instr,
   };
}

/**
 * \brief Returns a cursor set to before an instruction group.
 *
 * \param[in] igrp The instruction group.
 * \return The cursor.
 */
static inline pco_cursor pco_cursor_before_igrp(pco_igrp *igrp)
{
   return (pco_cursor){
      .option = PCO_CURSOR_BEFORE_IGRP,
      .igrp = igrp,
   };
}

/**
 * \brief Returns a cursor set to after an instruction group.
 *
 * \param[in] igrp The instruction group.
 * \return The cursor.
 */
static inline pco_cursor pco_cursor_after_igrp(pco_igrp *igrp)
{
   return (pco_cursor){
      .option = PCO_CURSOR_AFTER_IGRP,
      .igrp = igrp,
   };
}

/**
 * \brief Returns whether a cursor is set to before a construct.
 *
 * \param[in] cursor The cursor.
 * \return True if the cursor is set to before a construct.
 */
static inline bool pco_cursor_is_before(pco_cursor cursor)
{
   return cursor.option == PCO_CURSOR_BEFORE_CF_NODE ||
          cursor.option == PCO_CURSOR_BEFORE_INSTR ||
          cursor.option == PCO_CURSOR_BEFORE_IGRP;
}

/* Cursor get functions. */
/**
 * \brief Returns the function being pointed to by the cursor.
 *
 * \param[in] cursor The cursor.
 * \return The function being pointed to.
 */
static inline pco_func *pco_cursor_func(pco_cursor cursor)
{
   switch (cursor.option) {
   case PCO_CURSOR_BEFORE_CF_NODE:
   case PCO_CURSOR_AFTER_CF_NODE:
      switch (cursor.cf_node->type) {
      case PCO_CF_NODE_TYPE_BLOCK:
         return pco_cf_node_as_block(cursor.cf_node)->parent_func;

      case PCO_CF_NODE_TYPE_IF:
         return pco_cf_node_as_if(cursor.cf_node)->parent_func;

      case PCO_CF_NODE_TYPE_LOOP:
         return pco_cf_node_as_loop(cursor.cf_node)->parent_func;

      case PCO_CF_NODE_TYPE_FUNC:
         return pco_cf_node_as_func(cursor.cf_node);

      default:
         break;
      }
      break;

   case PCO_CURSOR_BEFORE_INSTR:
   case PCO_CURSOR_AFTER_INSTR:
      return cursor.instr->parent_func;

   case PCO_CURSOR_BEFORE_IGRP:
   case PCO_CURSOR_AFTER_IGRP:
      return cursor.igrp->parent_func;

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Returns the cf node being pointed to by the cursor.
 *
 * \param[in] cursor The cursor.
 * \return The cf node being pointed to.
 */
static inline pco_cf_node *pco_cursor_cf_node(pco_cursor cursor)
{
   switch (cursor.option) {
   case PCO_CURSOR_BEFORE_CF_NODE:
   case PCO_CURSOR_AFTER_CF_NODE:
      return cursor.cf_node;

   case PCO_CURSOR_BEFORE_INSTR:
   case PCO_CURSOR_AFTER_INSTR:
      return &cursor.instr->parent_block->cf_node;

   case PCO_CURSOR_BEFORE_IGRP:
   case PCO_CURSOR_AFTER_IGRP:
      return &cursor.igrp->parent_block->cf_node;

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Returns the block being pointed to by the cursor.
 *
 * \param[in] cursor The cursor.
 * \return The block being pointed to.
 */
static inline pco_block *pco_cursor_block(pco_cursor cursor)
{
   switch (cursor.option) {
   case PCO_CURSOR_BEFORE_CF_NODE:
   case PCO_CURSOR_AFTER_CF_NODE:
      switch (cursor.cf_node->type) {
      case PCO_CF_NODE_TYPE_BLOCK:
         return pco_cf_node_as_block(cursor.cf_node);

      /* TODO: other cf node types? */
      default:
         break;
      }
      break;

   case PCO_CURSOR_BEFORE_INSTR:
   case PCO_CURSOR_AFTER_INSTR:
      return cursor.instr->parent_block;

   case PCO_CURSOR_BEFORE_IGRP:
   case PCO_CURSOR_AFTER_IGRP:
      return cursor.igrp->parent_block;

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Returns the instruction being pointed to by the cursor.
 *
 * \param[in] cursor The cursor.
 * \return The instruction being pointed to.
 */
static inline pco_instr *pco_cursor_instr(pco_cursor cursor)
{
   bool before = pco_cursor_is_before(cursor);

   switch (cursor.option) {
   case PCO_CURSOR_BEFORE_CF_NODE:
   case PCO_CURSOR_AFTER_CF_NODE: {
      pco_block *block = NULL;

      switch (cursor.cf_node->type) {
      case PCO_CF_NODE_TYPE_BLOCK:
         block = pco_cf_node_as_block(cursor.cf_node);
         return before ? pco_first_instr(block) : pco_last_instr(block);

      /* TODO: other cf node types? */
      default:
         break;
      }
      break;
   }

   case PCO_CURSOR_BEFORE_INSTR:
   case PCO_CURSOR_AFTER_INSTR:
      return cursor.instr;

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Returns the instruction group being pointed to by the cursor.
 *
 * \param[in] cursor The cursor.
 * \return The instruction group being pointed to.
 */
static inline pco_igrp *pco_cursor_igrp(pco_cursor cursor)
{
   bool before = pco_cursor_is_before(cursor);

   switch (cursor.option) {
   case PCO_CURSOR_BEFORE_CF_NODE:
   case PCO_CURSOR_AFTER_CF_NODE: {
      pco_block *block = NULL;

      switch (cursor.cf_node->type) {
      case PCO_CF_NODE_TYPE_BLOCK:
         block = pco_cf_node_as_block(cursor.cf_node);
         /* Special case: we're in pco_group_instrs and want to go from
          * the start.
          */
         if (!block->parent_func->parent_shader->is_grouped)
            return NULL;
         return before ? pco_first_igrp(block) : pco_last_igrp(block);

      /* TODO: other cf node types? */
      default:
         break;
      }
      break;
   }

   case PCO_CURSOR_BEFORE_IGRP:
   case PCO_CURSOR_AFTER_IGRP:
      return cursor.igrp;

   default:
      break;
   }

   unreachable();
}

/* Builder functions. */
/**
 * \brief Creates a builder.
 *
 * \param[in] func The function being targeted.
 * \param[in] cursor The cursor.
 * \return The builder.
 */
static pco_builder pco_builder_create(pco_func *func, pco_cursor cursor)
{
   return (pco_builder){
      .func = func,
      .cursor = cursor,
   };
}

/**
 * \brief Inserts a block at a position specified by the builder.
 *
 * \param[in] b The builder.
 * \param[in] block The block.
 */
/* TODO: test with multiple blocks. */
static inline void pco_builder_insert_block(pco_builder *b, pco_block *block)
{
   struct list_head *list = &pco_cursor_cf_node(b->cursor)->link;
   bool before = pco_cursor_is_before(b->cursor);

   list_add(&block->cf_node.link, before ? list->prev : list);
   b->cursor = pco_cursor_after_block(block);
}

/**
 * \brief Inserts a instruction at a position specified by the builder.
 *
 * \param[in] b The builder.
 * \param[in] instr The instruction.
 */
static inline void pco_builder_insert_instr(pco_builder *b, pco_instr *instr)
{
   pco_instr *cursor_instr = pco_cursor_instr(b->cursor);
   bool before = pco_cursor_is_before(b->cursor);
   pco_block *block = pco_cursor_block(b->cursor);
   struct list_head *list = cursor_instr ? &cursor_instr->link : &block->instrs;

   instr->parent_block = block;

   list_add(&instr->link, (before && cursor_instr) ? list->prev : list);
   b->cursor = pco_cursor_after_instr(instr);
}

/**
 * \brief Inserts a instruction group at a position specified by the builder.
 *
 * \param[in] b The builder.
 * \param[in] igrp The instruction group.
 */
static inline void pco_builder_insert_igrp(pco_builder *b, pco_igrp *igrp)
{
   pco_igrp *cursor_igrp = pco_cursor_igrp(b->cursor);
   bool before = pco_cursor_is_before(b->cursor);
   pco_block *block = pco_cursor_block(b->cursor);
   struct list_head *list = cursor_igrp ? &cursor_igrp->link : &block->instrs;

   igrp->parent_block = block;

   list_add(&igrp->link, (before && cursor_igrp) ? list->prev : list);
   b->cursor = pco_cursor_after_igrp(igrp);
}

/* Generated op building functions. */
#include "pco_builder_ops.h"

/**
 * \brief Returns whether the instruction has the default execution condition.
 *
 * \param[in] instr The instruction.
 * \return True if the instruction has the default execution condition.
 */
static inline bool pco_instr_default_exec(pco_instr *instr)
{
   if (!pco_instr_has_exec_cnd(instr))
      return true;

   return pco_instr_get_exec_cnd(instr) == PCO_EXEC_CND_E1_ZX;
}
#endif /* PCO_BUILDER_H */
