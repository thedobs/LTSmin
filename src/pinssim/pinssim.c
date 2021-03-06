/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * pinssim.c - LTSmin
 *		Created by   Tobias J. Uebbing on 20140917
 *		Modified by  -
 *		Based on     LTSmin pins2lts-sym.c & lts-tracepp.c
 *		Copyright    Formal Methods & Tools
 *				     EEMCS faculty - University of Twente - 2014
 *		Supervisor 	 Jeroen Meijer
 *		Description  Simulator for LTSmin model checker
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *	 Includes & Definitions
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// Includes from pins2lts-sym.c
// TODO:
// Which of these are really necessary should be discussed later on

#include <hre/config.h>

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <alloca.h>
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/socket.h>

#include <dm/dm.h>
#include <hre/user.h>
// From lts.h:
#include <hre/runtime.h>
#include <lts-lib/lts.h>
#include <util-lib/chunk_support.h>
// -----------
#include <lts-io/user.h>
#include <pins-lib/pg-types.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/property-semantics.h>
#include <pins-lib/dlopen-api.h>
#include <pins-lib/dlopen-pins.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <spg-lib/spg-solve.h>
#include <vset-lib/vector_set.h>
#include <util-lib/dynamic-array.h>
#include <util-lib/bitset.h>
#include <hre/stringindex.h>

#ifdef HAVE_SYLVAN
#include <sylvan.h>
#else
#include <mc-lib/atomics.h>
#define LACE_ME
#endif

// Colors for console output if available:
#if defined(__unix__) || defined(__linux__) ||  defined(__APPLE__) || defined(__MACH__)
#define RESET   	"\033[0m"
#define BLACK   	"\033[30m"      	   /* Black */
#define RED     	"\033[31m"     		   /* Red */
#define GREEN   	"\033[32m"      	   /* Green */
#define YELLOW  	"\033[33m"     		   /* Yellow */
#define BLUE    	"\033[34m"  		   /* Blue */
#define MAGENTA 	"\033[35m"      	   /* Magenta */
#define CYAN    	"\033[36m"     		   /* Cyan */
#define WHITE   	"\033[37m"    		   /* White */
#define BOLDBLACK   "\033[1m\033[30m"      /* Bold Black */
#define BOLDRED     "\033[1m\033[31m"      /* Bold Red */
#define BOLDGREEN   "\033[1m\033[32m"      /* Bold Green */
#define BOLDYELLOW  "\033[1m\033[33m"      /* Bold Yellow */
#define BOLDBLUE    "\033[1m\033[34m"      /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m"      /* Bold Magenta */
#define BOLDCYAN    "\033[1m\033[36m"      /* Bold Cyan */
#define BOLDWHITE   "\033[1m\033[37m"      /* Bold White */
#else
#define RESET   	""
#define BLACK   	""      	   /* Black */
#define RED     	""     		   /* Red */
#define GREEN   	""      	   /* Green */
#define YELLOW  	""     		   /* Yellow */
#define BLUE    	""  		   /* Blue */
#define MAGENTA 	""      	   /* Magenta */
#define CYAN    	""     		   /* Cyan */
#define WHITE   	""    		   /* White */
#define BOLDBLACK   ""      /* Bold Black */
#define BOLDRED     ""      /* Bold Red */
#define BOLDGREEN   ""      /* Bold Green */
#define BOLDYELLOW  ""      /* Bold Yellow */
#define BOLDBLUE    ""      /* Bold Blue */
#define BOLDMAGENTA ""      /* Bold Magenta */
#define BOLDCYAN    ""      /* Bold Cyan */
#define BOLDWHITE   ""      /* Bold White */
#endif
#define BUFLEN 4096						   /* Buffer length for temporal buffer */

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 Structures & Variables
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// poptOption structure for HREaddOptions()
static  struct poptOption options[] = {
   	SPEC_POPT_OPTIONS,
  	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "PINS options",NULL},
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_options , 0 , "Vector set options",NULL},
    POPT_TABLEEND
};

// Trace node struct for the explored tree/trace data structure
typedef struct _trace_node{
	int grpNum;
	int * state;
	struct _trace_node * parent;
	int numSuccessors;
    struct _trace_node * successors;
    bool explored;
    bool isPath;
    int treeDepth;
} trace_node;

// Variables necessary for model checking
static lts_type_t ltstype;
static int N, nGrps,eLbls, sLbls, nGuards;
static int maxTreeDepth;
static model_t model;
static int * src;
static matrix_t * dm_r = NULL;
static matrix_t * dm_may_w = NULL;
static matrix_t * dm_must_w = NULL;
static matrix_t * stateLabel = NULL;
static trace_node * head;
static trace_node * current;

// Variables for loading a trace from model checker
static lts_t trace;
static int ** trace_states;
static int trace_nTrans;
static int * trace_trans = NULL;
static bool isLoaded = false;
static bool isGCF_ext = false;
static bool isPINsim_ext = false;

//Options
static bool loopDetection = false;
static bool clearMemOnGoBack = false;
static bool clearMemOnRestart = true;
static bool restartOnTraceLoad = true;
static bool colorOutput = true;

// I/O variables
FILE * opf;
static bool isWritingToFile;
static char * files[2]; 
static char * com[20];
static int numTextLines = 32;
static char * helpText[32] =
   {"COMMANDS:",
	"  help                           show all options",
	" PRINT OUT",
	"  current                        print info of current node",
	"  state                          print current state",
	"  trans                          print available transitions and subsequent states",
	"  path (states) (rw)             print path of transitions taken (with states and read-write dependencies)",
	"  tree (states)                  print tree of transitions explored (with states)",
	"  print2File                     print output to [FILE] (default:pinssim.out) instead to stdout",
	"             (start) [FILE]      start printing to [FILE] (default:pinssim.out)",
	"             (stop)              stop printing to output file",
	" STATE EXPLORATION",
	"  go / > [TRANSNUMBER]           take transition with TRANSNUMBER and explore potential successor states",
	"  goback / .. (clear/keep)       go back to parent state.",
	"                                 (and clears/keeps the state. See 'set' to change default)",
	"  restart (clear/keep)           restart exploration from initial state",
	"                                 (and clears/keeps the explored states. See 'set' to change default)",
	"  load [TYPE] [FILE]             load [TYPE] information from [FILE]",
	"       trace  [file.gcf]         load trace generated by LTSmin model checker and replays it",
	"       trace  [file.pinssim]     load previously explored/generated trace and replays it",
	"  save [TYPE] [FILE]             save [TYPE] information to [FILE]",
	"       trace  [file.pinssim]     save current trace to file",
	"  replay [ACTION]            	  replays [ACTION] if available",
	"         trace                   replays trace previously loaded from a file",
	" SETTINGS",
	"  set [OPTION] [VALUE]           Set OPTION to VALUE. OPTIONS:",
	"      loopDetection              true/false - default: false",
	"      clearMemOnGoBack           true/false - default: false",
	"      clearMemOnRestart          true/false - default: true",
	"      restartOnTraceLoad         true/false - default: true",
	" EXIT",
	"  quit / q                       quit PINSsim"};

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *	 Print out functionalities
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*printHelp()
  prints the help text contained in the helpTest array to the stdout*/
void
printHelp(){
	for (int i = 0; i < numTextLines; i++) fprintf(stdout, "%s\n", helpText[i]);
}

/*printDMrow()
  prints the row of the dependency matrix (Read & Write dependencies) to the current output
  with the same spacing as the according state*/
void
printDMrow(trace_node * node){
	char rw[N];
	for (int i = 0; i < N; i++){
		// Determine type of RW dependency
		if (dm_is_set(dm_r, node->grpNum, i) && (dm_is_set(dm_may_w, node->grpNum, i))) {
            rw[i] = '+';
        } else if (dm_is_set(dm_r, node->grpNum, i)) {
            rw[i] = 'r';
        } else if (dm_is_set(dm_must_w, node->grpNum, i)) {
            rw[i] = 'w';
        } else if (dm_is_set(dm_may_w, node->grpNum, i)) {
            rw[i] = 'W';
        } else {
            rw[i] = '-';
        }
		// Add space according to the amount of space that the state takes  
		int absVal = node->state[i];
		if (absVal < 0) absVal *= -1;
		if (node->state[i]>=10000) fprintf(opf, "    ");
		else if (node->state[i]>=1000) fprintf(opf, "   ");
		else if (node->state[i]>=100) fprintf(opf, "  ");
		else if (node->state[i]>=10) fprintf(opf, " ");
	 	fprintf(opf, "%c,",  rw[i]);
	}
	fprintf(opf,"\n");
}

/*printState()
  prints state representation optional with read and write dependecies and 
  with higlights where node->state differes from compare_state*/
void 
printState(trace_node * node, int * compare_state, bool withRW){
	if(withRW) printDMrow(node);
	for (int i = 0; i < N; i++){
			if(compare_state != NULL && colorOutput){
				if(node->state[i] != compare_state[i]) fprintf(opf, YELLOW "%d" RESET ",", node->state[i]);
				else fprintf(opf, "%d,", node->state[i]);
			} else fprintf(opf, "%d,", node->state[i]);
	}
	fprintf(opf,"\n");
}

/*printTransistions()
  prints all transistion groups available from this node optional with their successor states
  or indicates that from here no transitions are available*/
void 
printTransitions(trace_node * node, bool withSuccStates){

	//fprintf(opf,"\n");
	if(node->numSuccessors > 0){ 
		fprintf(opf, CYAN "Transitions available from here:\n" RESET);
		for (int i = 0; i < node->numSuccessors; i++){
			int absVal = node->successors[i].grpNum;
			if (absVal < 0) absVal *= -1;
			if(absVal<10)fprintf(opf, "group     %d - ", node->successors[i].grpNum);
			else if(absVal<100)fprintf(opf, "group    %d - ", node->successors[i].grpNum);
			else if(absVal<1000)fprintf(opf, "group   %d - ", node->successors[i].grpNum);
			else if(absVal<10000)fprintf(opf, "group  %d - ", node->successors[i].grpNum);
			else if(absVal<100000)fprintf(opf, "group %d - ", node->successors[i].grpNum);
			if (withSuccStates) printState(&(node->successors[i]),node->state,0);
		}
		if (!withSuccStates) fprintf(opf,"\n");
	}
	else if (colorOutput) fprintf(opf, " No transitions available from here!\n " CYAN "INFO: " RESET "Enter 'goback' or '..' to go back to the parent state.parent\n");
	else fprintf(opf, " No transitions available from here!\n INFO: Enter 'goback' or '..' to go back to the parent state.parent\n");
}

/*printNode()
  prints the content of a trace_node struct to the current output*/
void 
printNode(trace_node * node, bool withSuccStates){
	//fprintf(opf, "\n");
	if (colorOutput) fprintf(opf, BOLDWHITE "---- NODE -----------------------\n" RESET);
	else fprintf(opf, "---- NODE -----------------------\n");
	if(node->grpNum >= 0){
		if (colorOutput) fprintf(opf, CYAN "State of parent node:\n" RESET);
		else fprintf(opf,"State of parent node:\n");
		fprintf(opf, "              ");
		printState(node->parent,node->state,0);
	}
	else{
		if (colorOutput) fprintf(opf, "-> This is the " GREEN "INITIAL" RESET " state!\n");
		else fprintf(opf, "-> This is the INITIAL state!\n");
	}
	if (colorOutput) fprintf(opf, CYAN "State of this node:\n" RESET);
	else fprintf(opf, "State of this node:\n");
	fprintf(opf, "              ");
	printState(node,NULL,0);
	printTransitions(node, withSuccStates);
	if(node->grpNum >= 0){ 
		if (colorOutput) fprintf(opf, CYAN "Transition that lead here: " RESET "%d ", node->grpNum);
		else fprintf(opf, "Transition that lead here: %d ", node->grpNum);
	}
	if (colorOutput) fprintf(opf, CYAN "Tree depth: " RESET "%d\n", node->treeDepth);
	else fprintf(opf, "Tree depth: %d\n", node->treeDepth);
	if (colorOutput) fprintf(opf, BOLDWHITE "---- END NODE -------------------\n" RESET);
	else fprintf(opf, "---- END NODE -------------------\n");
	//fprintf(opf, "\n");
}

/*printTrace()
  prints the trace of transition groups from start node to the initial state optional with 
  state representation and/or read & write dependencies*/
void 
printTrace(trace_node * start, bool withStates, bool withRW){

	trace_node * temp = start;

	if (colorOutput){
		fprintf(opf, "Trace from " RED " CURRENT" RESET " to " GREEN "INITIAL" RESET " state:\n\n");
		fprintf(opf, RED "CURRENT" RESET);
	}
	else {
		fprintf(opf, "Trace from CURRENT to INITIAL state:\n\n");
		fprintf(opf, "CURRENT");
	}

	if(withStates) fprintf(opf, "\n");
	while(temp->grpNum >= 0){
		if(withStates){
			if (withRW) printState(temp,temp->parent->state,1);
			else printState(temp,temp->parent->state, 0);
			fprintf(opf,"  |\n");
			fprintf(opf, "%d\n", temp->grpNum);
			fprintf(opf,"  |\n");
		}
		else{
			fprintf(opf, " %d ", temp->grpNum);
			fprintf(opf, "->");
		}
		temp = temp->parent;
	}
	if (temp->grpNum < 0){
		if(withStates){
			printState(temp,NULL,0);
			if (colorOutput) fprintf(opf, GREEN "INITIAL\n" RESET);
			else fprintf(opf, "INITIAL\n");
		} else{ 
			if (colorOutput) fprintf(opf, GREEN "INITIAL\n" RESET);
			else fprintf(opf, "INITIAL\n");
		}
	}

}

/*printTreeNode()
  Helper function for printTree() printing a node according to its content.*/
static void
printTreeNode(trace_node * node, bool withStates, char * sign){
	for (int i = 0; i < node->treeDepth; i++) fprintf(opf, " ");
	if(node->isPath){ 
		if (colorOutput) fprintf(opf, CYAN "%s " RESET, sign);
		else fprintf(opf,"-> %s ", sign);
	}
	else fprintf(opf, "%s ", sign);
	if (node->grpNum >= 0) fprintf(opf, "%d ", node->grpNum);
	else{ 
		if (colorOutput) fprintf(opf, GREEN "INITAL " RESET);
		else fprintf(opf, "INITAL ");
	}
	if(withStates){ 
		fprintf(opf, "- ");
		if (node->grpNum >= 0) printState(node,node->parent->state,0);
		else printState(node,NULL,0);
	}
	else fprintf(opf, "\n");
}

/*printTree()
  Prints a simple tree of 'node' and all its explored successors recursively.
  Showing the transition group only by default. If 'withStates' is true
  also the states will be printed.*/
static void
printTree(trace_node * node, bool withStates, char * sign){
	printTreeNode(node,withStates, sign);
	for (int i = 0; i < node->numSuccessors; i++){
		if(i == node->numSuccessors-1) printTree(&(node->successors[i]),withStates,"\\");
		else printTree(&(node->successors[i]),withStates,"|");
	}
}

/*print2File()
  sets the opf FILE pointer from stdout to a output file and reversely
  furthermore it toggles the colorOutput option for the PRINT functions*/
static void
print2File(char * file, bool writeToFile){
	if(writeToFile){
		if (isWritingToFile) fclose(opf);
		if (file != NULL) opf = fopen(file,"w");
		else opf = fopen("pinssim.out","w");
		isWritingToFile = true;
		colorOutput = false;
		fprintf(stdout, "Started writing print out to file.\n");
	} else {
		fclose(opf);
		opf = stdout;
		isWritingToFile = false;
		colorOutput = true;
		fprintf(stdout, "Stopped writing print out to file.\n");
	}
}

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 Memory deallocation & node reset functionalities
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*freeTreeMem()
 Frees the memory of a trace_node tree/trace completely from and including 'node' on*/
void 
freeTreeMem(trace_node * node){
	if (node->numSuccessors > 0){
		for (int i = 0; i < node->numSuccessors; i++){
			freeTreeMem(&(node->successors[i]));
		}
		free(node->successors);
		if(node->parent != NULL) node->parent->numSuccessors -= 1;
		if(node->parent == NULL) free(node);
		return;
	}
	else{
		if(node->parent != NULL) node->parent->numSuccessors -= 1;
		if(node->parent == NULL) free(node);
		return;
	} 
}

/*resetNode()
  resets node to be unexplored again*/
static void 
resetNode(trace_node * node){
	for (int i = 0; i < node->numSuccessors; i++){
		for (int j = 0; j < node->successors[i].numSuccessors; j++){
			freeTreeMem(&(node->successors[i].successors[j]));
		}
		free(node->successors);
		node->numSuccessors = 0;
		node->explored = false;
	}
}

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 State exploration functionalities
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*array2PtrArray()
  Convertes and stores the int array to a int * ptrArray*/
static void
array2PtrArray(int array[], int size, int * ptrArray){
	for (int i = 0; i < size; ++i) ptrArray[i] = array[i];
}

/*isSameState() - compares two int * arrays and returns true if they are the same*/
static bool
isSameState(int * s1, int * s2){
	for (int i = 0; i < N; i++) if(s1[i] != s2[i]) return false;
	return true;
}

/*detectLoops()
  Checks whether 'comp->states' occured before in 'start' and its successors*/
static void
detectLoops(trace_node * start, trace_node * comp){
	if(isSameState(start->state,comp->state) && start->treeDepth != comp->treeDepth){
		fprintf(stdout, CYAN "-> Found reoccurence of state:\n" RESET);
		printNode(start,0);
		printNode(comp,0);
	}
	if(start->numSuccessors > 0){
		for (int i = 0; i < start->numSuccessors; i++) detectLoops(&(start->successors[i]),comp);
	}
}

/*group_add()
 Call back function necessary for GBgetTransitionLong(). 
 Creates successor nodes and adds pointers to the the parent node. 
 Checks whether the new explored states occurred before in the search tree
 if 'loopDetection' is set to true by the user.*/
static void
group_add(void *context, transition_info_t *ti, int *dst, int *cpy){
	trace_node * node = (trace_node*)context;
	node->numSuccessors += 1;
	if (node->numSuccessors==1) node->successors = malloc(sizeof(trace_node)*node->numSuccessors);
	else  node->successors = realloc(node->successors,sizeof(trace_node)*node->numSuccessors);

	if (node->treeDepth+1 > maxTreeDepth) maxTreeDepth = node->treeDepth+1;

	node->successors[node->numSuccessors-1].grpNum = ti->group;
	node->successors[node->numSuccessors-1].state = (int*)malloc(sizeof(int)*N);
	memcpy(node->successors[node->numSuccessors-1].state,dst,sizeof(int)*N);
	node->successors[node->numSuccessors-1].parent = node;
	node->successors[node->numSuccessors-1].numSuccessors = 0;
	node->successors[node->numSuccessors-1].explored = 0;
	node->successors[node->numSuccessors-1].isPath = 0;
	node->successors[node->numSuccessors-1].treeDepth = node->treeDepth+1;
 	
 	if(loopDetection) detectLoops(head,&(node->successors[node->numSuccessors-1]));

}

/*explore_states()
  Explores potential successor states of the current node by going through all possible transitions.
  Does not explore an previously explored node again.*/
void
explore_states(trace_node * node, bool printOut){
	
	if(node->explored == 0){
		if (node->grpNum >= 0 && printOut) fprintf(stdout,  MAGENTA "Taking transition %d - exploring potential successor states.\n\n" RESET, node->grpNum);
		for (int i = 0; i < nGrps; i++){
			 GBgetTransitionsLong(model,i,node->state,group_add,node);
		}
		node->explored = 1;
	}
	else fprintf(stdout, CYAN "\n\t INFO:" RESET " Successors already explored!\n\n");
	node->isPath = true;
	if (printOut) printNode(node, 1);
}

/*proceed()
 proceeds from the current node to its successor with index in the successor array.
 Explores successor states of the new current node*/
bool 
proceed(int transNum, bool printOut){
	int i = 0;
	int numSucc = current->numSuccessors;
	while (i < numSucc){
		if(current->successors[i].grpNum == transNum){
			current = &(current->successors[i]);
			explore_states(current,printOut);
			break;
		}
		i++;
	}
	if (i >= numSucc){ 
		fprintf(stderr, RED "\t ERROR: " RESET " this transition is not available from this state.\n\t Enter 'trans' to see all available transitions and there states.\n");
		return false;
	} else return true;
}

/*proceedByState()
 proceeds from the current node to its successor with index in the successor array.
 Explores successor states of the new current node*/
bool 
proceedByState(int * state, bool printOut){
	int i = 0;
	int numSucc = current->numSuccessors;
	while (i < numSucc){
		if(isSameState(current->successors[i].state,state)){
			current = &(current->successors[i]);
			explore_states(current,printOut);
			break;
		}
		i++;
	}
	if (i >= numSucc){ 
		fprintf(stderr, RED "\t ERROR: " RESET " this state is not as successor of this state.\n\t Enter 'trans' to see all available transitions and there states.\n");
		return false;
	} else return true;
}

/*goBack()
  Returns from successor to parent node if possible and resets successor node as unexplored*/
void
goBack(bool clearMem){
	if(current->grpNum != -1){
		if (clearMem && current->explored) resetNode(current);
		current->isPath = false;
		current = current->parent;
		fprintf(stdout,  MAGENTA "Going back to parent state.\n\n" RESET);
		printNode(current,1);
	}
	else fprintf(stdout, CYAN "\t INFO:" RESET " This is the INITIAL state! You can't go back!\n");
}

/*restart()
  Sets current back to head (INITIAL state) and optional 
  also deletes the previously explored tree/trace*/
void
restart(bool clearMem){
	current = head;
	if(clearMem){
		for (int i = 0; i < current->numSuccessors; i++){
			resetNode(&(current->successors[i]));
			current->successors[i].isPath = false;
		}
	}
	fprintf(stdout,  MAGENTA "Going back to " RESET GREEN "INITIAL" RESET MAGENTA " state.\n\n" RESET);
	printNode(current, 1);
}

/*replayTransitions()
  Attempts to take all transition in the 'transNumbers' array after each other.
  Stops if a transition is at the current state not available.*/
void 
replayTransitions(int transNumbers[], int amountOf, bool printOut){
	int i = 0;
	bool success = true;
	while (i < amountOf && success){
		success = proceed(transNumbers[i],false);
		if(!success) break;
		i++;
	}
	if(!success){ 
		fprintf(stderr, RED "\t ERROR: " RESET "Stopped replaying transitions. Transition at index %d is not available in this state.\n",i);
	 	fprintf(stdout,     "\t        Enter 'trans' to see all available transitions and successor states.\n");
		fprintf(stderr,     "\t        Make sure the current is the correct initial state for this trace.\n");
	} else if (printOut) {
		fprintf(stdout, "\n");
		fprintf(stdout, MAGENTA "Took %d transitions leading to following state:\n" RESET, amountOf);
		printNode(current,1);
	}
}

/*replayTransitionsByStates()
  Attempts to take all transition in the 'transNumbers' array after each other.
  Stops if a transition is at the current state not available.*/
void 
replayTransitionsByStates(int ** states, int amountOf, bool printOut){
	int * succ_state = (int*)alloca(N*sizeof(int));
	array2PtrArray(states[0],N,succ_state);
	int i;
	if(isSameState(succ_state,current->state)) i = 1;
	else i = 0;
	
	bool success = true;
	while (i < amountOf && success){
		array2PtrArray(states[i],N,succ_state);
		success = proceedByState(succ_state,false);
		if(!success) break;
		i++;
	}
	if(!success){ 
		fprintf(stderr, RED "\t ERROR: " RESET "Stopped replaying transitions. Transition at index %d is not available in this state.\n",i);
	 	fprintf(stderr,     "\t        Enter 'trans' to see all available transitions and successor states.\n");
		fprintf(stderr,     "\t        Make sure the current is correct initial state for this trace.\n");
	} else if (printOut) {
		fprintf(stdout, "\n");
		fprintf(stdout, MAGENTA "Took %d transitions leading to following state:\n" RESET, amountOf);
		printNode(current,1);
	}
}

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 Saving or loading to/from files
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*removeExtension()
  removes current file extension from file (including '.') and stores the result in 'minusExt'*/
void
removeExtension(char * file, char * minusExt, int index){
	for(int i = 0; i < index; i++){
		minusExt[i]=file[i];
	}
}

/*loadPINSimFile()
  loads and chunks data from a csv/pinssim 'file' into int * values
  returns an int that stores the number of values*/
int
loadPINSsimFile(char * file, int * values){
	FILE * read;
	long lSize;
	char * buffer;
	size_t result;

	read = fopen(file, "rb" );
	if (read==NULL) {fprintf(stderr, RED "\t ERROR:"RESET" File error"); exit (1);}
	
	// obtain file size:
	fseek(read , 0 , SEEK_END);
	lSize = ftell (read);
	rewind(read);

	// allocate memory to contain the whole file:
	buffer = (char*) malloc (sizeof(char)*lSize);
	if (buffer == NULL) {fprintf(stderr, RED "\t ERROR:"RESET" Memory allocation error"); exit (2);}
	// copy the file into the buffer:
	result = fread (buffer,1,lSize,read);
	if ((long)result != lSize) {fprintf(stderr, RED "\t ERROR:" RESET "Reading error"); exit (3);}

	// terminate
	fclose(read);

	int nSemi = 0;
	for(int i = 0; buffer[i] != '\0'; ++i){
		if (buffer[i] == ';') nSemi++;
	}

	// Tokenize buffer and store result in int * values
	int n = 0;
	if(nSemi > 0) values = (int*)realloc(values,nSemi*sizeof(int));
	if(strstr(buffer,";")){
		char * split = strtok(buffer,";");
		sscanf(split,"%d",&values[n]);
		while (split!=NULL){
			split = strtok(NULL,";");
			if(split!=NULL){
				n++;
				sscanf(split,"%d",&values[n]);
			}
		}
	}
	else{
		sscanf(buffer,"%d",&values[n]);
	}

	free(buffer);

	return (nSemi);
}

/*saveTracePINSsim()
  saves current trace in .pinssim file format to 'file'*/
bool
saveTracePINSsim(char * file){
	FILE * save = fopen(file,"w");
	trace_node * temp = head;
	bool foundEnd = false;
	int numTransitions = 0;
	fprintf(save, "%d;", N);
	fprintf(save, "%d;", eLbls);
	fprintf(save, "%d;", sLbls);
	fprintf(save, "%d;", current->treeDepth);
	while (!foundEnd){
		int i = 0;
		while (i < temp->numSuccessors){
			if(temp->successors[i].isPath){
				fprintf(save, "%d;", temp->successors[i].grpNum);
				break;
			}
			i++;
		}
		if (i >= temp->numSuccessors) foundEnd = true;
		else{ 
			numTransitions++;
			temp = &temp->successors[i];
		}
	}
	if (current->treeDepth == numTransitions){ 
		fprintf(stdout,CYAN "\t INFO: " RESET " Saved trace with %d transitions to file: %s \n\n",numTransitions,file);
		fclose(save);
		return true;
	} else {
		fprintf(stderr, RED "\t ERROR:" RESET " Expected %d, found %d transitions\n", current->treeDepth,numTransitions);
		fclose(save);
		return false;
	}
}

/*saveTrace()
  stores current trace in 'file' to be later loaded and replayed again.
  Checks on and ensures valid file extension before saving.
  For now supported: .pinssim Default: .pinssim*/
void
saveTrace(char * file){

	char * extension = "";
	extension = strrchr (file, '.');
	if (extension == NULL) strcat(file,".pinssim");
	else if (strcmp(extension,".pinssim") != 0 && strcmp(extension,".gcf") != 0){
		int index = extension-file; 
		char * temp = (char*)alloca((index-1)*sizeof(char));
		removeExtension(file,temp,index);
		file = temp;
		strcat(file,".pinssim");
		fprintf(stdout,CYAN "\t INFO: " RESET "Detected unsuitable file extension %s.\n", extension);
		fprintf(stdout,     "\t       Replaced detected extension with .pinssim.\n\n");
	}

	extension = strrchr (file, '.');
	if (strcmp(extension,".pinssim") == 0){
		saveTracePINSsim(file);
	} 
	else if (strcmp(extension,".gcf") == 0){
		fprintf(stdout,CYAN "\t INFO: " RESET " Saving to .gcf files not supported yet. Pls use .pinssim.\n\n");
	}
}

/*loadTracePINSsim() 
  loads a trace previously stored by PINSsim in a .pinssim file with the help of loadPINSsimFile()
  and checks whether parameters of stored trace match the parameters of the current model.
  Resets the current to the initial state depending on the bool restartOnTraceLoad*/
bool
loadTracePINSsim(char * file){
	
	int n;
	int * values = (int*)calloc((N+4),sizeof(int));
	char * extension = "";
	extension = strrchr (file, '.');

	if (extension == NULL){ 
		fprintf(stderr, RED "\t ERROR:" RESET " No file extension. Please load a .gcf or .pinssim file.\n");
		free(values);
		return false;
	} 
	else if (strcmp(extension,".pinssim") == 0){
		n = loadPINSsimFile(file,values);
		fprintf(stdout, CYAN "\t INFO: " RESET " Loaded data from file: %s \n\n",file);
	} else{
		fprintf(stderr, RED "\t ERROR:" RESET " File extension %s unknown. Please load a .pinssim file.\n", extension);
		free(values);
		return false;
	}
	
	if (n >= 4){
		if (values[0] != N ||
		    values[1] != eLbls ||
			values[2] != sLbls ||
			values[3] != (n-4) ){
			fprintf(stderr, RED "\t ERROR:" RESET " This trace does not match the loaded model.\n");
			free(values);
			return false;
		} else{
			trace_nTrans = values[3];
			trace_trans = (int*)malloc(trace_nTrans*sizeof(int));
			for (int i = 0; i < trace_nTrans; ++i){
				trace_trans[i] = values[4+i];
			}
			fprintf(stdout, "\n");
			fprintf(stdout, CYAN "\t INFO: " RESET "Loaded trace with %d transitions from %s\n", trace_nTrans, file);
			// TODO: Remove reset of explored nodes to INITIAL maybe for future use
			if (restartOnTraceLoad && !isSameState(current->state,head->state)) restart(true);
			free(values);
			return true;
		}
	}
	else{
		fprintf(stderr, RED "\t ERROR:" RESET " This file contains to less data to be a .pinssim file.\n");
		free(values);
		return false;
	}
}

/*loadTraceGCF()
  loads lts_t trace from file and extracts a int ** array of the states in the trace from it. 
  Checks whether parameters of loaded trace match parameters of current model.
  Resets the current to the initial state depending on the bool restartOnTraceLoad*/
bool
loadTraceGCF(char * file){
	opf = stdout;
	fprintf(stdout, "\n");
	char * extension = "";
	extension = strrchr (file, '.');
	if (extension == NULL){ 
		fprintf(stderr, RED "\t ERROR:" RESET " No file extension. Please load a .gcf or .pinssim file.\n");
		return false;
	} 
	else if (strcmp(extension,".gcf") == 0){
		trace = lts_create();
		lts_read(file,trace);
		fprintf(stdout,CYAN "\t INFO: " RESET " Loaded trace from file: %s \n\n",file);
	} else{
		fprintf(stderr, RED "\t ERROR:" RESET " File extension %s unknown. Please load a .gcf file.\n", extension);
		return false;
	}
	if (lts_type_get_state_length(trace->ltstype) != N ||
	    lts_type_get_edge_label_count(trace->ltstype) != eLbls ||
		lts_type_get_state_label_count(trace->ltstype) != sLbls){
		fprintf(stderr, RED "\t ERROR:" RESET " This trace does not match the loaded model.\n");
		return false;
	} else{
		fprintf(stdout, CYAN "\t INFO:" RESET " Parameteres of trace match model.\n");
		trace_states = (int**)malloc(trace->transitions*sizeof(int*));
		for(uint32_t x=0; x<=trace->transitions; ++x) {
            	uint32_t i = (x != trace->transitions ? x : trace->dest[x-1]);
            	trace_states[i] = (int*)malloc(N*sizeof(int));
            	if (N){
            		int temp[N];
            		TreeUnfold(trace->state_db, i, temp);
            		array2PtrArray(temp,N,trace_states[i]);
            	}
       	}
       	// TODO: Remove reset of explored nodes to INITIAL maybe for future use
       	if (restartOnTraceLoad && !isSameState(current->state,head->state)) restart(true);
       	return true;
    }
}

/*loadTrace()
  loads and replays a trace from 'file' path if the located file has a .gcf or .pinssim extension*/
void
loadTrace(char * file){
	char * extension = "";
	extension = strrchr (file, '.');

	if (extension == NULL){ 
		fprintf(stderr, RED "\t ERROR:" RESET " No file extension. Please load a .gcf or .pinssim file.\n");
	}
	else if (strcmp(extension,".gcf") == 0){
		if(loadTraceGCF(file)){
			if (isLoaded && isPINsim_ext) isPINsim_ext = false;
			isLoaded = true;
			isGCF_ext = true;
			replayTransitionsByStates(trace_states,(int)trace->transitions,true);
		}
		else fprintf(stderr, RED "\t ERROR:" RESET " Loading trace from file '%s' was not successful.\n", file);
	}
	else if (strcmp(extension,".pinssim") == 0){
		if (loadTracePINSsim(file)){
			if (isLoaded && isGCF_ext) isGCF_ext = false; 
			isLoaded = true;
			isPINsim_ext = true;
			replayTransitions(trace_trans,trace_nTrans,true);
		}
	} else{
		fprintf(stderr, RED "\t ERROR:" RESET " File extension %s unknown. Please load a .gcf or .pinssim file.\n", extension);
	}
}

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 Shell interface functionalities
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*parseComLine()
 splits a char array into separate char arrays by the separator*/
int parseComLine(char * separator, char * line){
	int n = 0;
	if(strstr(line,separator)){
		char * split = strtok(line,separator);
		com[n] = split;
		while (split!=NULL && n < 20){
			split = strtok(NULL,separator);
			n++;
			com[n] = split;	
		}
		if (n >= 20) fprintf(stdout,CYAN "\t INFO: " RESET "Max amount of command tokens is 20. Tokens %d and higher will not be considered.\n\n",n);
	}
	else{
		com[n] = line;
		n++;
	}
	return n;
}

/*handleIO()
  Takes a input a char pointer array read from the console. Splits it up in separate commands.
  Executes functioanlities according to commands and arguments.*/
bool handleIO(char * input){

	int nCom = parseComLine(" ", input);

	//////////////////////////////////////////
	// PRINT OUT COMMANDS
	if (strcmp(com[0],"help") == 0) printHelp();
	else if (strcmp(com[0],"current") == 0) printNode(current, 1);
	else if (strcmp(com[0],"state") == 0) printState(current,NULL,0);
	else if (strcmp(com[0],"trans") == 0) printTransitions(current, 1);
	else if (strcmp(com[0],"trace") == 0){
		if (nCom >= 2){
			if(strcmp(com[1],"states") == 0){
				if (nCom >= 3 && strcmp(com[2],"rw") == 0) printTrace(current, 1, 1);
				else printTrace(current, 1, 0);
			} else fprintf(stderr, RED "\t ERROR: " RESET " Entered argument for 'path' unknown\n\t See 'help' for description.\n");
		} else printTrace(current, 0, 0);
	}
	else if (strcmp(com[0],"tree") == 0){
		if (nCom >= 2){
			if(strcmp(com[1],"states") == 0) printTree(head, 1, "\\");
			else fprintf(stderr, RED "\t ERROR: " RESET " Entered argument for 'tree' unknown\n\t See 'help' for description.\n");
		} else printTree(head, 0, "\\");
	}
	else if (strcmp(com[0],"print2file") == 0){
		if (nCom >= 2){
			if(strcmp(com[1],"start") == 0){
				if (nCom >= 3) print2File(com[2],true);
				else print2File(NULL,true);
			}
			else if(strcmp(com[1],"stop") == 0) print2File(NULL,false);
		} else {
			if (isWritingToFile) print2File(NULL,false);
			else print2File(NULL,true);
		}
	}
	//////////////////////////////////////////
	// EXPLORATION COMMANDS
	else if (strcmp(com[0],"go") == 0 || strcmp(com[0],">") == 0){
		if (nCom >= 2){
			int transNum;
			sscanf(com[1],"%d",&transNum);
			proceed(transNum,true);
		} else fprintf(stderr, RED "\t ERROR: " RESET " 'go' needs as argument the number of the transition to take.\n");
	}
	else if (strcmp(com[0],"goback") == 0 || strcmp(com[0],"..") == 0){ 
		if (nCom >= 2){
			if (strcmp(com[1],"clear") == 0) goBack(1);
			else if (strcmp(com[1],"keep") == 0) goBack(0);
			else fprintf(stderr, RED "\t ERROR: " RESET " unknown argument for 'goback'.\n");
		} else goBack(clearMemOnGoBack);
	}
	else if (strcmp(com[0],"restart") == 0){
		if (nCom >= 2){
			if (strcmp(com[1],"clear") == 0) restart(1);
			else if (strcmp(com[1],"keep") == 0) restart(0);
			else fprintf(stderr, RED "\t ERROR: " RESET " unknown argument for 'restart'.\n");
		} else restart(clearMemOnRestart);
	}
	else if (strcmp(com[0],"load") == 0){
		if (nCom >= 2){
			if (strcmp(com[1],"trace") == 0){
				if (nCom >= 3){
					loadTrace(com[2]);		
				}
			} else fprintf(stderr, RED "\t ERROR: " RESET " 'load trace' needs as 2. argument the path fo the file of the trace that should be loaded.\n");
		} else fprintf(stderr, RED "\t ERROR: " RESET " 'load' needs as argument a specification what to load.\n");
	}
	else if (strcmp(com[0],"save") == 0){
		if (nCom >= 2){
			if (strcmp(com[1],"trace") == 0){
				if (nCom >= 3){
					saveTrace(com[2]);
				} else fprintf(stderr, RED "\t ERROR: " RESET " 'save trace' needs as 2. argument the path fo the file of the trace that should be loaded.\n");
			} 
		} else fprintf(stderr, RED "\t ERROR: " RESET " 'save' needs as argument a specification what to load.\n");
	}
	else if (strcmp(com[0],"replay") == 0){
		if (nCom >= 2){
			if (strcmp(com[1],"trace") == 0){
				if (isLoaded){
					if (isGCF_ext) replayTransitionsByStates(trace_states,(int)trace->transitions,true);
					if (isPINsim_ext) replayTransitions(trace_trans,trace_nTrans,true);
				}
				else fprintf(stderr, RED "\t ERROR: " RESET "There is no trace loaded yet. Use 'load trace' to read a trace from a file.\n");
			} 
		} else fprintf(stderr, RED "\t ERROR: " RESET "'replay' needs as argument a specification what should be replayed.\n");
	}
	//////////////////////////////////////////
	// CHANGE SETTING COMMAND
	else if (strcmp(com[0],"set") == 0){
		if(nCom >= 3){
			if (strcmp(com[1],"loopDetection") == 0){ 
				if(strcmp(com[2],"true") == 0 || strcmp(com[2],"1") == 0){
					loopDetection = 1;
					fprintf(stdout, CYAN "\t set" RESET " loopDetection = true\n");
				}
				if(strcmp(com[2],"false") == 0 || strcmp(com[2],"0") == 0){
					loopDetection = 0;
					fprintf(stdout, CYAN "\t set" RESET " loopDetection = false\n");
				}
			}
			else if (strcmp(com[1],"clearMemOnGoBack") == 0){ 
				if(strcmp(com[2],"true") == 0 || strcmp(com[2],"1") == 0){
					clearMemOnGoBack = 1;
					fprintf(stdout, CYAN "\t set" RESET " clearMemOnGoBack = true\n");
				}
				if(strcmp(com[2],"false") == 0 || strcmp(com[2],"0") == 0){
					clearMemOnGoBack = 0;
					fprintf(stdout, CYAN "\t set" RESET " clearMemOnGoBack = false\n");
				}
			}
			else if (strcmp(com[1],"clearMemOnRestart") == 0){ 
				if(strcmp(com[2],"true") == 0 || strcmp(com[2],"1") == 0){
					clearMemOnRestart = 1;
					fprintf(stdout, CYAN "\t set" RESET " clearMemOnRestart = true\n");
				}
				if(strcmp(com[2],"false") == 0 || strcmp(com[2],"0") == 0){
					clearMemOnRestart = 0;
					fprintf(stdout, CYAN "\t set" RESET " clearMemOnRestart = false\n");
				}
			}
			else if (strcmp(com[1],"restartOnTraceLoad") == 0){ 
				if(strcmp(com[2],"true") == 0 || strcmp(com[2],"1") == 0){
					restartOnTraceLoad = 1;
					fprintf(stdout, CYAN "\t set" RESET " restartOnTraceLoad = true\n");
				}
				if(strcmp(com[2],"false") == 0 || strcmp(com[2],"0") == 0){
					restartOnTraceLoad = 0;
					fprintf(stdout, CYAN "\t set" RESET " restartOnTraceLoad = false\n");
				}
			}
		}
		else fprintf(stderr, RED "\t ERROR: " RESET " 'set' needs a option name and a values as argument.\n\t See 'help' for description.\n");
	}
	//////////////////////////////////////////
	// EXIT COMMAND & DEFAULT ERROR
	else if ((strcmp(com[0],"q") == 0)||(strcmp(com[0],"quit") == 0)) return 0;
	else fprintf(stderr, RED "\t ERROR: " RESET " Command unknown - enter 'help' for available options.\n");
	return 1;
}

/*runIO()*/
void runIO(){

	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	bool run = true;

	fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
	fprintf(stdout, CYAN "\nEnter command below - 'help' for options - 'quit' to quit:\n" RESET);

	while((read = getline(&line, &len, stdin)) != -1 && run){
		if(read > 0){
			//printf("\n-> read %zd chars from stdin, allocated %zd bytes for line %s",read,len,line);
			line[strlen(line)-1] = '\0';
			fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n\n");
			run = handleIO(line);
			if (!run) break;
		}
		fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
		fprintf(stdout, CYAN "\nEnter command below - 'help' for options - 'quit' to quit:\n" RESET);
	}
	//free (line);
}

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 Main - execution
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int main (int argc, char *argv[]){

	opf = stdout;

	fprintf(stdout, CYAN "\n Start up PINSsim\n" RESET);
	fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
	
	// Initate model and HRE
	HREinitBegin(argv[0]);
    HREaddOptions(options,"representation of the input\n\nOptions");
    lts_lib_setup(); // add options for LTS library

    HREinitStart(&argc,&argv,1,2,files,"<model> [<etf>]");

	// Warning(info, "opening %s", files[0]);
    model = GBcreateBase();

    GBsetChunkMethods(model,HREgreyboxNewmap,HREglobal(),
                      HREgreyboxI2C,
                      HREgreyboxC2I,
                      HREgreyboxCAtI,
                      HREgreyboxCount);

    // Load model from file
    GBloadFile(model, files[0], &model);
    fprintf(stdout,CYAN "INFO: " RESET " Loaded file: %s \n",files[0]);
  
    // if (argc >= 2){
    // 	opf = fopen(files[1],"w");
    // 	if (opf != NULL){ 
    // 		GBprintDependencyMatrixCombined(opf, model);
    // 		fprintf(stdout, CYAN "INFO:" RESET " Saved dependency matrix to file: %s \n",files[1]);
    // 	}
    // 	//GBprintStateLabelMatrix(opf, model);
    // }

    // Initialize global variables
    dm_r = GBgetExpandMatrix(model);	        //Dependency matrices
    dm_may_w = GBgetDMInfoMayWrite(model);
    dm_must_w = GBgetDMInfoMustWrite(model);
    stateLabel = GBgetStateLabelInfo(model);	// State labels

    ltstype = GBgetLTStype(model);				//LTS Type
    N = lts_type_get_state_length(ltstype);		//State length
    eLbls = lts_type_get_edge_label_count(ltstype);
    sLbls = lts_type_get_state_label_count(ltstype);
    nGrps = dm_nrows(GBgetDMInfo(model));
    if (GBhasGuardsInfo(model)){
        nGuards = GBgetStateLabelGroupInfo(model, GB_SL_ALL)->count;
        fprintf(stdout,CYAN "INFO: " RESET " Number of guards %d.\n", nGuards);
    }
        
  	fprintf(stdout,CYAN "INFO: " RESET " State vector length is %d; there are %d groups\n", N, nGrps);

    src = (int*)alloca(sizeof(int)*N);
    GBgetInitialState(model, src);

    maxTreeDepth = 0;

    head = (trace_node*)malloc(sizeof(trace_node));
    head->grpNum = -1;
    head->state = (int*)malloc(sizeof(int)*N);
    head->state = src;
    head->parent = NULL;
    head->numSuccessors = 0;
    head->explored = 0;
    head->treeDepth = maxTreeDepth;

    fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
    fprintf(stdout, GREEN "\nINITIAL " RESET "state:\n");
    explore_states(head,true);

    // Set current node to head
    current = head;	    
    
    // Check for further run time arguments and load or open file
    if (argc > 2){
		fprintf(stdout, "\n");
		char * extension = "";
		extension = strrchr (files[1], '.');

		if (!(extension == NULL)){ 
			if (strcmp(extension,".gcf") == 0 || strcmp(extension,".pinssim") == 0){
    			loadTrace(files[1]);
    		}
    	}
    }

    // Start IO procedure
	runIO();

	// Close output file if PINSsim still printing to it
	if(isWritingToFile) fclose(opf);

	// Free allocated memory before exit 
	freeTreeMem(head);
	head = NULL;
	current = NULL;
	// TODO: free trace, trace_states?
	dm_free(dm_r);
	dm_free(dm_may_w);
	dm_free(dm_must_w);
	//dm_free(stateLabel);

	return 0;
}

