struct exec_list;

bool do_mat_op_to_vec(struct exec_list *instructions);

extern "C" {
	bool c_do_mat_op_to_vec(struct exec_list *instructions) {
		return do_mat_op_to_vec(instructions);
	}
};
