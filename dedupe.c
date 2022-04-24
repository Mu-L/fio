#include "fio.h"

/**
 * initializes the global dedup workset.
 * this needs to be called after all jobs' seeds
 * have been initialized
 */
int init_global_dedupe_working_set_seeds(void)
{
	int i;
	struct thread_data *td;

	for_each_td(td, i) {
		if (!td->o.dedupe_global)
			continue;

		if (init_dedupe_working_set_seeds(td, 1))
			return 1;
	}

	return 0;
}

int init_dedupe_working_set_seeds(struct thread_data *td, bool global_dedup)
{
	int tindex;
	struct thread_data *td_seed;
	unsigned long long i, j, num_seed_advancements, pages_per_seed;
	struct frand_state dedupe_working_set_state = {0};

	if (!td->o.dedupe_percentage || !(td->o.dedupe_mode == DEDUPE_MODE_WORKING_SET))
		return 0;

	tindex = td->thread_number - 1;
	num_seed_advancements = td->o.min_bs[DDIR_WRITE] /
		min_not_zero(td->o.min_bs[DDIR_WRITE], (unsigned long long) td->o.compress_chunk);
	/*
	 * The dedupe working set keeps seeds of unique data (generated by buf_state).
	 * Dedupe-ed pages will be generated using those seeds.
	 */
	td->num_unique_pages = (td->o.size * (unsigned long long)td->o.dedupe_working_set_percentage / 100) / td->o.min_bs[DDIR_WRITE];
	td->dedupe_working_set_states = malloc(sizeof(struct frand_state) * td->num_unique_pages);
	if (!td->dedupe_working_set_states) {
		log_err("fio: could not allocate dedupe working set\n");
		return 1;
	}

	frand_copy(&dedupe_working_set_state, &td->buf_state);
	frand_copy(&td->dedupe_working_set_states[0], &dedupe_working_set_state);
	pages_per_seed = max(td->num_unique_pages / thread_number, 1ull);
	for (i = 1; i < td->num_unique_pages; i++) {
		/*
		 * When compression is used the seed is advanced multiple times to
		 * generate the buffer. We want to regenerate the same buffer when
		 * deduping against this page
		 */
		for (j = 0; j < num_seed_advancements; j++)
			__get_next_seed(&dedupe_working_set_state);

		/*
		 * When global dedup is used, we rotate the seeds to allow
		 * generating same buffers across different jobs. Deduplication buffers
		 * are spread evenly across jobs participating in global dedupe
		 */
		if (global_dedup && i % pages_per_seed == 0) {
			td_seed = tnumber_to_td(++tindex % thread_number);
			frand_copy(&dedupe_working_set_state, &td_seed->buf_state);
		}

		frand_copy(&td->dedupe_working_set_states[i], &dedupe_working_set_state);
	}

	return 0;
}
