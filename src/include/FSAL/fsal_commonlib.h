/*
 * fsal common utility functions
 */

/* fsal_module to fsal_namespace helpers
 */

int fsal_attach_namespace(struct fsal_module *fsal_hdl,
			  struct glist_head *obj_link);
void fsal_detach_namespace(struct fsal_module *fsal_hdl,
			   struct glist_head *obj_link);

/* fsal_namespace common methods
 */

struct exportlist;

int fsal_namespace_init(struct fsal_namespace *, struct exportlist *);

void free_namespace_ops(struct fsal_namespace *namespace);

/* fsal_obj_handle common methods
 */

int fsal_obj_handle_init(struct fsal_obj_handle *, struct fsal_namespace *,
			 object_file_type_t);

int fsal_obj_handle_uninit(struct fsal_obj_handle *obj);

/*
 * pNFS DS Helpers
 */

int fsal_attach_ds(struct fsal_namespace *namespace,
		   struct glist_head *ds_link);
void fsal_detach_ds(struct fsal_namespace *namespace,
		    struct glist_head *ds_link);
int fsal_ds_handle_init(struct fsal_ds_handle *, struct fsal_ds_ops *,
			struct fsal_namespace *);
int fsal_ds_handle_uninit(struct fsal_ds_handle *ds);
