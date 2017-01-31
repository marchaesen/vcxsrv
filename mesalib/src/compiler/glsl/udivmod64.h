ir_function_signature *
udivmod64(void *mem_ctx, ir_factory &body)
{
   ir_function_signature *const sig =
      new(mem_ctx) ir_function_signature(glsl_type::uvec4_type);
   exec_list sig_parameters;

   ir_variable *const r0001 = new(mem_ctx) ir_variable(glsl_type::uvec2_type, "numer", ir_var_function_in);
   sig_parameters.push_tail(r0001);
   ir_variable *const r0002 = new(mem_ctx) ir_variable(glsl_type::uvec2_type, "denom", ir_var_function_in);
   sig_parameters.push_tail(r0002);
   ir_variable *const r0003 = new(mem_ctx) ir_variable(glsl_type::int_type, "i", ir_var_auto);
   body.emit(r0003);
   ir_variable *const r0004 = new(mem_ctx) ir_variable(glsl_type::uint64_t_type, "n64", ir_var_auto);
   body.emit(r0004);
   ir_variable *const r0005 = new(mem_ctx) ir_variable(glsl_type::int_type, "log2_denom", ir_var_auto);
   body.emit(r0005);
   ir_variable *const r0006 = new(mem_ctx) ir_variable(glsl_type::uvec2_type, "quot", ir_var_auto);
   body.emit(r0006);
   body.emit(assign(r0006, ir_constant::zero(mem_ctx, glsl_type::uvec2_type), 0x03));

   ir_expression *const r0007 = expr(ir_unop_find_msb, swizzle_y(r0002));
   body.emit(assign(r0005, add(r0007, body.constant(int(32))), 0x01));

   /* IF CONDITION */
   ir_expression *const r0009 = equal(swizzle_y(r0002), body.constant(0u));
   ir_expression *const r000A = nequal(swizzle_y(r0001), body.constant(0u));
   ir_expression *const r000B = logic_and(r0009, r000A);
   ir_if *f0008 = new(mem_ctx) ir_if(operand(r000B).val);
   exec_list *const f0008_parent_instructions = body.instructions;

      /* THEN INSTRUCTIONS */
      body.instructions = &f0008->then_instructions;

      ir_variable *const r000C = new(mem_ctx) ir_variable(glsl_type::int_type, "i", ir_var_auto);
      body.emit(r000C);
      ir_variable *const r000D = body.make_temp(glsl_type::int_type, "findMSB_retval");
      body.emit(assign(r000D, expr(ir_unop_find_msb, swizzle_x(r0002)), 0x01));

      body.emit(assign(r0005, r000D, 0x01));

      body.emit(assign(r000C, sub(body.constant(int(31)), r000D), 0x01));

      /* LOOP BEGIN */
      ir_loop *f000E = new(mem_ctx) ir_loop();
      exec_list *const f000E_parent_instructions = body.instructions;

         body.instructions = &f000E->body_instructions;

         /* IF CONDITION */
         ir_expression *const r0010 = less(r000C, body.constant(int(1)));
         ir_if *f000F = new(mem_ctx) ir_if(operand(r0010).val);
         exec_list *const f000F_parent_instructions = body.instructions;

            /* THEN INSTRUCTIONS */
            body.instructions = &f000F->then_instructions;

            body.emit(new(mem_ctx) ir_loop_jump(ir_loop_jump::jump_break));


         body.instructions = f000F_parent_instructions;
         body.emit(f000F);

         /* END IF */

         /* IF CONDITION */
         ir_expression *const r0012 = lshift(swizzle_x(r0002), r000C);
         ir_expression *const r0013 = lequal(r0012, swizzle_y(r0001));
         ir_if *f0011 = new(mem_ctx) ir_if(operand(r0013).val);
         exec_list *const f0011_parent_instructions = body.instructions;

            /* THEN INSTRUCTIONS */
            body.instructions = &f0011->then_instructions;

            ir_expression *const r0014 = lshift(swizzle_x(r0002), r000C);
            body.emit(assign(r0001, sub(swizzle_y(r0001), r0014), 0x02));

            ir_expression *const r0015 = lshift(body.constant(1u), r000C);
            body.emit(assign(r0006, bit_or(swizzle_y(r0006), r0015), 0x02));


         body.instructions = f0011_parent_instructions;
         body.emit(f0011);

         /* END IF */

         body.emit(assign(r000C, add(r000C, body.constant(int(-1))), 0x01));

      /* LOOP END */

      body.instructions = f000E_parent_instructions;
      body.emit(f000E);

      /* IF CONDITION */
      ir_expression *const r0017 = lequal(swizzle_x(r0002), swizzle_y(r0001));
      ir_if *f0016 = new(mem_ctx) ir_if(operand(r0017).val);
      exec_list *const f0016_parent_instructions = body.instructions;

         /* THEN INSTRUCTIONS */
         body.instructions = &f0016->then_instructions;

         body.emit(assign(r0001, sub(swizzle_y(r0001), swizzle_x(r0002)), 0x02));

         body.emit(assign(r0006, bit_or(swizzle_y(r0006), body.constant(1u)), 0x02));


      body.instructions = f0016_parent_instructions;
      body.emit(f0016);

      /* END IF */


   body.instructions = f0008_parent_instructions;
   body.emit(f0008);

   /* END IF */

   body.emit(assign(r0004, expr(ir_unop_pack_uint_2x32, r0001), 0x01));

   ir_expression *const r0018 = sub(body.constant(int(63)), r0005);
   body.emit(assign(r0003, expr(ir_binop_min, body.constant(int(31)), r0018), 0x01));

   /* LOOP BEGIN */
   ir_loop *f0019 = new(mem_ctx) ir_loop();
   exec_list *const f0019_parent_instructions = body.instructions;

      body.instructions = &f0019->body_instructions;

      /* IF CONDITION */
      ir_expression *const r001B = less(r0003, body.constant(int(1)));
      ir_if *f001A = new(mem_ctx) ir_if(operand(r001B).val);
      exec_list *const f001A_parent_instructions = body.instructions;

         /* THEN INSTRUCTIONS */
         body.instructions = &f001A->then_instructions;

         body.emit(new(mem_ctx) ir_loop_jump(ir_loop_jump::jump_break));


      body.instructions = f001A_parent_instructions;
      body.emit(f001A);

      /* END IF */

      ir_variable *const r001C = body.make_temp(glsl_type::uint64_t_type, "assignment_tmp");
      ir_expression *const r001D = expr(ir_unop_pack_uint_2x32, r0002);
      body.emit(assign(r001C, lshift(r001D, r0003), 0x01));

      /* IF CONDITION */
      ir_expression *const r001F = lequal(r001C, r0004);
      ir_if *f001E = new(mem_ctx) ir_if(operand(r001F).val);
      exec_list *const f001E_parent_instructions = body.instructions;

         /* THEN INSTRUCTIONS */
         body.instructions = &f001E->then_instructions;

         body.emit(assign(r0004, sub(r0004, r001C), 0x01));

         ir_expression *const r0020 = lshift(body.constant(1u), r0003);
         body.emit(assign(r0006, bit_or(swizzle_x(r0006), r0020), 0x01));


      body.instructions = f001E_parent_instructions;
      body.emit(f001E);

      /* END IF */

      body.emit(assign(r0003, add(r0003, body.constant(int(-1))), 0x01));

   /* LOOP END */

   body.instructions = f0019_parent_instructions;
   body.emit(f0019);

   ir_variable *const r0021 = body.make_temp(glsl_type::uint64_t_type, "packUint2x32_retval");
   body.emit(assign(r0021, expr(ir_unop_pack_uint_2x32, r0002), 0x01));

   /* IF CONDITION */
   ir_expression *const r0023 = lequal(r0021, r0004);
   ir_if *f0022 = new(mem_ctx) ir_if(operand(r0023).val);
   exec_list *const f0022_parent_instructions = body.instructions;

      /* THEN INSTRUCTIONS */
      body.instructions = &f0022->then_instructions;

      ir_expression *const r0024 = expr(ir_unop_pack_uint_2x32, r0002);
      body.emit(assign(r0004, sub(r0004, r0024), 0x01));

      body.emit(assign(r0006, bit_or(swizzle_x(r0006), body.constant(1u)), 0x01));


   body.instructions = f0022_parent_instructions;
   body.emit(f0022);

   /* END IF */

   ir_variable *const r0025 = body.make_temp(glsl_type::uvec4_type, "vec_ctor");
   body.emit(assign(r0025, r0006, 0x03));

   body.emit(assign(r0025, expr(ir_unop_unpack_uint_2x32, r0004), 0x0c));

   body.emit(ret(r0025));

   sig->replace_parameters(&sig_parameters);
   return sig;
}
