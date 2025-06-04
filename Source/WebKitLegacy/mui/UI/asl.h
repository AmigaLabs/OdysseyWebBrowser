#ifndef __ASL_H__
#define __ASL_H__

char * asl_run(CONST_STRPTR p, struct TagItem *tags, ULONG remember_path);
ULONG  asl_run_multiple(CONST_STRPTR p, struct TagItem *tags, char ***files, ULONG remember_path);
void   asl_free(ULONG count, char **files);

#endif
