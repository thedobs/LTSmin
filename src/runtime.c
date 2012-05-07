#include <config.h>
#undef _XOPEN_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include <hre-main.h>
#include <runtime.h>
#include <unix.h>

int RTverbosity=1;

int linear_search(const si_map_entry map[],const char*key){
	while(map[0].key){
		if(!strcmp(map[0].key,key)) return map[0].val;
		map++;
	}
	return -1;
}

char *key_search(si_map_entry map[],const int val){
    while(map[0].key){
        if(map[0].val == val) return map[0].key;
        map++;
    }
    return "not found";
}

void (*RThandleFatal)(const char*file,int line,int errnum,int code);

void RTinit(int *argcp,char**argvp[]){
    RThandleFatal=NULL;
    HREinitBare(argcp,argvp);
}

void RTparseOptions(const char* argline,int *argc_p,const char***argv_p){
	int len=strlen(argline)+8;
	char cmdline[len];
	sprintf(cmdline,"fake %s",argline);
    // argv is allocated as one block by poptParseArgvString
	int res=poptParseArgvString(cmdline,argc_p,argv_p);
	if (res){
		Fatal(1,error,"could not parse %s: %s",cmdline,poptStrerror(res));
	}
    (*argv_p)[0]=strdup(get_label());
}

void RTexitHelp(int exit_code){
    HREprintHelp();
    exit(exit_code);
}

void RTinitPopt(int *argc_p,char**argv_p[],const struct poptOption * options,
	int min_args,int max_args,char*args[],
	const char* pgm_prefix,const char* arg_help,const char* extra_help
){
    (void) pgm_prefix;
    RTinit(argc_p,argv_p);
    HREaddOptions(options,extra_help);
    HREparseOptions(*argc_p,*argv_p,min_args,max_args,args,arg_help);
    if (!log_active(infoShort)) RTverbosity=0;
    if (log_active(infoLong)) RTverbosity=2;
}

char* RTinitNextArg(){
    return HREnextArg();
}

void* RTmalloc(size_t size){
	if(size==0) return NULL;
	void *tmp=malloc(size);
	if (tmp==NULL) Fatal(0,error,"out of memory trying to get %zu bytes",size);
	return tmp;
}

void* RTmallocZero(size_t size){
    if(size==0) return NULL;
	void *p = calloc(size, 1);
    if (p==NULL) Fatal(0,error,"out of memory trying to get %zu bytes",size);
	return p;
}

void* RTalign(size_t align, size_t size)
{
    void *ret;
    errno = posix_memalign(&ret, align, size);
    if (errno) {
    switch (errno) {
        case ENOMEM:
            Fatal(0,error,"out of memory on allocating %zu bytes aligned at %d",
                  size, align);
        case EINVAL:
            Fatal(0,error,"invalid alignment %d", align);
        default:
            Fatal(0,error,"unknown error allocating %zu bytes aligned at %d",
                  size, align);
    }}
    assert(NULL != ret);
    return ret;
}

void* RTrealloc(void *rt_ptr, size_t size){
    void *tmp=realloc(rt_ptr,size);
    if (tmp==NULL && size !=0) Fatal(0,error,"out of memory trying to resize to %zu bytes",
                         size);
    return tmp;
}

#define MAX_ALIGN_MEMSET (1024*1024)
#define MAX_ALIGN_ZEROS (1024)
static size_t next_calloc = 0;
static void *calloc_table[MAX_ALIGN_ZEROS][2];

void* RTalignZero(size_t align, size_t size) {
    if (0 == align) Abort("Zero alignment in RTalignZero");
    if (0 == size) return NULL;
    if (size < MAX_ALIGN_MEMSET) {
        // for small sizes do memset
        void *mem = RTalign(align, size);
        memset (mem, 0 , size);
        return mem;
    }
    // for large sizes use calloc and do manual alignment
    if ((size / align)*align != size) // make size multiple of align
        size = ((size + align) / align)*align;
    void *p = calloc((size / align + 1), align);
    size_t pp = (size_t)p;
    if ((pp / align) * align != pp) { // manual alignment only if needed
        void *old = p;
        p = (void*)((pp / align + 1) * align);
        // store self-aligned allocs in calloc_table
        size_t next = __sync_fetch_and_add (&next_calloc, 1);
        calloc_table[next][0] = p;
        calloc_table[next][1] = old;
    }
    return p;
}

char* RTstrdup(const char *str){
    if (str == NULL) return NULL;
    char *tmp = strdup (str);
    if (tmp == NULL) Fatal(0, error, "out of memory trying to get %zu bytes",
                           strlen (str)+1);
    return tmp;
}

void RTfree(void *rt_ptr){
	for (size_t i = 0; i < next_calloc; i++) {
	    if (rt_ptr == calloc_table[i][0]) {
	        free (calloc_table[i][1]);
	        return;
	    }
	}
    if (rt_ptr != NULL) {
	    free (rt_ptr);
	}
}

void *
RTtrydlsym (void *handle, const char *symbol)
{
    return dlsym (handle, symbol);
}

void *
RTdlsym (const char *libname, void *handle, const char *symbol)
{
    void *ret = RTtrydlsym (handle, symbol);
    if (ret == NULL) {
        const char *dlerr = dlerror ();
        Fatal (1, error, "dynamically loading from `%s': %s",
               libname,
               dlerr != NULL ? dlerr : "unknown error");
    }
    return ret;
}

void *
RTtryedlsym (const char *libname, void *handle, const char *symbol)
{
    void *ret = dlsym (handle, symbol);
    if (ret == NULL) {
        const char *dlerr = dlerror ();
        Fatal (1, error, "dynamically loading from `%s': %s",
               libname,
               dlerr != NULL ? dlerr : "unknown error");
    }
    return ret;
}

/* From stefan's HRE code: */
#if defined(__APPLE__)

size_t RTmemSize() {
    int mib[4];
    int64_t physical_memory;
    size_t len = sizeof(int64_t);
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    len = sizeof(int64_t);
    sysctl(mib, 2, &physical_memory, &len, NULL, 0);
    return physical_memory;
}

#else

size_t RTmemSize() {
    long res=sysconf(_SC_PHYS_PAGES);
    size_t pagesz=RTpageSize();
    return pagesz*((size_t)res);
}

#endif

int RTnumCPUs() {
    long res=sysconf(_SC_NPROCESSORS_ONLN);
    return (size_t)res;
}

size_t RTpageSize() {
    long res=sysconf(_SC_PAGESIZE);
    return (size_t)res;
}

