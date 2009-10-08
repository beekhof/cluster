#include "fsck.h"
#include "fsck_incore.h"
#include "fs_inode.h"
#include "bio.h"
#include "inode.h"
#include "util.h"
#include "block_list.h"
#include "metawalk.h"

static int remove_eattr_entry(struct fsck_sb *sdp, osi_buf_t *leaf_bh,
			struct gfs_ea_header *curr,
			struct gfs_ea_header *prev)
{
	if(!prev)
		curr->ea_type = GFS_EATYPE_UNUSED;
	else {
		uint32_t tmp32 = gfs32_to_cpu(curr->ea_rec_len) +
			gfs32_to_cpu(prev->ea_rec_len);
		prev->ea_rec_len = cpu_to_gfs32(tmp32);
		if (curr->ea_flags & GFS_EAFLAG_LAST)
			prev->ea_flags |= GFS_EAFLAG_LAST;
	}
	if(write_buf(sdp, leaf_bh, 0)){
		stack;
		log_err("EA removal failed.\n");
		return -1;
	}
	log_err("Bad Extended Attribute at block #%"PRIu64" removed.\n",
		 BH_BLKNO(leaf_bh));
	return 0;
}

static int ask_remove_eattr_entry(struct fsck_sb *sdp, osi_buf_t *leaf_bh,
				  struct gfs_ea_header *curr,
				  struct gfs_ea_header *prev,
				  int fix_curr, int fix_curr_len)
{
	errors_found++;
	if (query(sdp, "Remove the bad Extended Attribute? (y/n) ")) {
		if (fix_curr)
			curr->ea_flags |= GFS_EAFLAG_LAST;
		if (fix_curr_len) {
			uint32_t max_size = sdp->sb.sb_bsize;
			uint32_t offset = (uint32_t)(((unsigned long)curr) -
					     ((unsigned long)leaf_bh->b_data));
			curr->ea_rec_len = cpu_to_be32(max_size - offset);
		}
		if (remove_eattr_entry(sdp, leaf_bh, curr, prev)) {
			stack;
			return -1;
		}
		errors_corrected++;
	} else {
		log_err("Bad Extended Attribute not removed.\n");
	}
	return 1;
}

static int ask_remove_eattr(struct fsck_inode *ip)
{
	errors_found++;
	if (query(ip->i_sbd, "Remove the bad Extended Attribute? (y/n) ")) {
		ip->i_di.di_eattr = 0;
		if (fs_copyout_dinode(ip))
			log_err("Bad Extended Attribute could not be "
				"removed.\n");
		else {
			errors_corrected++;
			log_err("Bad Extended Attribute removed.\n");
		}
	} else
		log_err("Bad Extended Attribute not removed.\n");
	return 1;
}

static int check_eattr_indir(struct fsck_inode *ip, uint64_t block,
			     uint64_t parent, osi_buf_t **bh,
			     void *private)
{
	int *update = (int *) private;
	struct fsck_sb *sbp = ip->i_sbd;
	struct block_query q;
	osi_buf_t *indir_bh = NULL;

	*update = 0;
	if(check_range(sbp, block)) {
		log_err("Extended attributes indirect block #%llu"
			" for inode #%llu is out of range.\n",
			(unsigned long long)block,
			(unsigned long long)ip->i_num.no_addr);
		return ask_remove_eattr(ip);
	}
	else if (block_check(sbp->bl, block, &q)) {
		stack;
		return -1;
	}
	else if(q.block_type != indir_blk) {
		log_err("Extended attributes indirect block #%llu"
			" for inode #%llu is invalid.\n",
			(unsigned long long)block,
			(unsigned long long)ip->i_num.no_addr);
		return ask_remove_eattr(ip);
	}
	else if(get_and_read_buf(sbp, block, &indir_bh, 0)){
		log_warn("Unable to read Extended Attribute leaf block "
			 "#%"PRIu64".\n", block);
		return ask_remove_eattr(ip);
	}

	*bh = indir_bh;
	return 0;
}

static int check_eattr_leaf(struct fsck_inode *ip, uint64_t block,
			    uint64_t parent, osi_buf_t **bh, void *private)
{
	struct fsck_sb *sbp = ip->i_sbd;
	struct block_query q;
	osi_buf_t *leaf_bh;

	if(check_range(sbp, block)) {
		log_err("Extended attributes leaf block #%"PRIu64
			" for inode #%" PRIu64 " is out of range.\n",
			block, ip->i_num.no_addr);
		return ask_remove_eattr(ip);
	}
	else if (block_check(sbp->bl, block, &q)) {
		stack;
		return -1;
	}
	else if(q.block_type != meta_eattr) {
		log_err("Extended attributes leaf block #%"PRIu64
			" for inode #%" PRIu64 " is invalid.\n",
			block, ip->i_num.no_addr);
		return ask_remove_eattr(ip);
	}
	else if(get_and_read_buf(sbp, block, &leaf_bh, 0)){
		log_warn("Unable to read Extended attributes leaf block "
			 "#%"PRIu64".\n", block);
		return ask_remove_eattr(ip);
	}

	*bh = leaf_bh;
	return 0;
}

static int check_eattr_entry(struct fsck_inode *ip,
			     osi_buf_t *leaf_bh,
			     struct gfs_ea_header *ea_hdr,
			     struct gfs_ea_header *ea_hdr_prev,
			     void *private)
{
	struct fsck_sb *sdp = ip->i_sbd;
	char ea_name[256];
	uint32_t offset = (uint32_t)(((unsigned long)ea_hdr) -
			                  ((unsigned long)BH_DATA(leaf_bh)));
	uint32_t max_size = sdp->sb.sb_bsize;
	if(!ea_hdr->ea_rec_len){
		log_err("EA has rec length == 0\n");
		return ask_remove_eattr_entry(sdp, leaf_bh, ea_hdr,
					      ea_hdr_prev, 1, 1);
	}
	if(offset + gfs32_to_cpu(ea_hdr->ea_rec_len) > max_size){
		log_err("EA rec length too long\n");
		return ask_remove_eattr_entry(sdp, leaf_bh, ea_hdr,
					      ea_hdr_prev, 1, 1);
	}
	if(offset + gfs32_to_cpu(ea_hdr->ea_rec_len) == max_size &&
	   (ea_hdr->ea_flags & GFS_EAFLAG_LAST) == 0){
		log_err("last EA has no last entry flag\n");
		return ask_remove_eattr_entry(sdp, leaf_bh, ea_hdr,
					      ea_hdr_prev, 0, 0);
	}
	if(!ea_hdr->ea_name_len){
		log_err("EA has name length == 0\n");
		return ask_remove_eattr_entry(sdp, leaf_bh, ea_hdr,
					      ea_hdr_prev, 0, 0);
	}

	memset(ea_name, 0, sizeof(ea_name));
	strncpy(ea_name, (char *)ea_hdr + sizeof(struct gfs_ea_header),
		ea_hdr->ea_name_len);

	if(!GFS_EATYPE_VALID(ea_hdr->ea_type) &&
	   ((ea_hdr_prev) || (!ea_hdr_prev && ea_hdr->ea_type))){
		log_err("EA (%s) type is invalid (%d > %d).\n",
			ea_name, ea_hdr->ea_type, GFS_EATYPE_LAST);
		return ask_remove_eattr_entry(sdp, leaf_bh, ea_hdr,
					      ea_hdr_prev, 0, 0);
	}

	if(ea_hdr->ea_num_ptrs){
		uint32 avail_size;
		int max_ptrs;

		avail_size = sdp->sb.sb_bsize - sizeof(struct gfs_meta_header);
		max_ptrs = (gfs32_to_cpu(ea_hdr->ea_data_len)+avail_size-1)/avail_size;

		if(max_ptrs > ea_hdr->ea_num_ptrs){
			log_err("EA (%s) has incorrect number of pointers.\n", ea_name);
			log_err("  Required:  %d\n"
				"  Reported:  %d\n",
				max_ptrs, ea_hdr->ea_num_ptrs);
			return ask_remove_eattr_entry(sdp, leaf_bh, ea_hdr,
						      ea_hdr_prev, 0, 0);
		} else {
			log_debug("  Pointers Required: %d\n"
				  "  Pointers Reported: %d\n",
				  max_ptrs, ea_hdr->ea_num_ptrs);
		}
	}
	return 0;
}

static int check_eattr_extentry(struct fsck_inode *ip, uint64_t *ea_ptr,
			 osi_buf_t *leaf_bh,
			 struct gfs_ea_header *ea_hdr,
			 struct gfs_ea_header *ea_hdr_prev,
			 void *private)
{
	struct block_query q;
	struct fsck_sb *sbp = ip->i_sbd;

	if(block_check(sbp->bl, gfs64_to_cpu(*ea_ptr), &q)) {
		stack;
		return -1;
	}
	if(q.block_type != meta_eattr) {
		if(remove_eattr_entry(sbp, leaf_bh, ea_hdr, ea_hdr_prev)){
			stack;
			return -1;
		}
		return 1;
	}
	return 0;
}

/* Go over all inodes with extended attributes and verify the EAs are
 * valid */
int pass1c(struct fsck_sb *sbp)
{
	uint64_t block_no = 0;
	osi_buf_t *bh;
	struct fsck_inode *ip = NULL;
	int update = 0;
	struct metawalk_fxns pass1c_fxns = { 0 };
	int error = 0;

	pass1c_fxns.check_eattr_indir = &check_eattr_indir;
	pass1c_fxns.check_eattr_leaf = &check_eattr_leaf;
	pass1c_fxns.check_eattr_entry = &check_eattr_entry;
	pass1c_fxns.check_eattr_extentry = &check_eattr_extentry;
	pass1c_fxns.private = (void *) &update;

	log_info("Looking for inodes containing ea blocks...\n");
	while (!find_next_block_type(sbp->bl, eattr_block, &block_no)) {

		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			return FSCK_OK;
		log_info("EA in inode %"PRIu64"\n", block_no);
		if(get_and_read_buf(sbp, block_no, &bh, 0)) {
			stack;
			return FSCK_ERROR;
		}
		if(copyin_inode(sbp, bh, &ip)) {
			stack;
			return FSCK_ERROR;
		}

		block_unmark(sbp->bl, block_no, eattr_block);
		log_debug("Found eattr at %"PRIu64"\n", ip->i_di.di_eattr);
		/* FIXME: Handle walking the eattr here */
		error = check_inode_eattr(ip, &pass1c_fxns);
		if(error < 0) {
			stack;
			return FSCK_ERROR;
		}

		if(update) {
			gfs_dinode_out(&ip->i_di, BH_DATA(bh));
			write_buf(sbp, bh, 0);
		}

		free_inode(&ip);
		relse_buf(sbp, bh);

		block_no++;
	}
	return FSCK_OK;
}
