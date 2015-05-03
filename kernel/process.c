#include "process.h"
#include "klibc.h"
#include "file.h"
#include "global_defs.h"
#include "loader.h"
#include "vm.h"
#include "elf.h"
static uint32_t GLOBAL_PID;

uint32_t sample_func(uint32_t);

int process_global_init() {
	pcb_table = kmalloc(MAX_PROCESSES * 4);
	os_memset(pcb_table, 0, sizeof(int) * MAX_PROCESSES);
	GLOBAL_PID = 0;
	return 0;
}

pcb * process_create_from_file(char * file, char * arg) {

#define START 0x20000
#define PROC_LOCATION 0x9ff00000

	struct stats fstats;
	int fd = kopen(file, 'r');
	uint32_t start = START + PROC_LOCATION;
	uint32_t len;

	get_stats(file, &fstats);

	len = fstats.size;

	for (int i = 0; i < (len / BLOCK_SIZE) + 1; i++) {
		uint32_t *v = (uint32_t*) (start + (i * BLOCK_SIZE));
		vm_allocate_page(KERNEL_VAS, (void*) v, VM_PERM_USER_RW);
	}

	int counter = 0;
	int* location = (int*) start;
	while (counter < len) {
		kread(fd, location, 4);
		location += 1;
		counter += 4;
	}

	return process_create((uint32_t*) start, len, arg);
}

/*Spring 2015 course_os: Sathya Sankaran, Rakan Stanbouly, Jason Sim

 creates a process and initializes the PCB
 returns pcb pointer upon success
 returns 0 if there is no more room in pcb table
 file_p is a file pointer that we will create the process with */
pcb* process_create(uint32_t* file_p, uint32_t len, char * arg) {

	uint32_t* free_space_in_pcb_table = process_next_free_slot_in_pcb_table();

	//This used to be == 0, which doesn't seem correct
	if (free_space_in_pcb_table != 0) {
		pcb* pcb_pointer = (pcb*) kmalloc(sizeof(pcb));

		pcb_pointer->len = len;
		pcb_pointer->start = file_p;
		pcb_pointer->arg = arg;
		//Create the process VAS here so that we can use it when allocating process memory
		pcb_pointer->stored_vas = vm_new_vas();

		//Load the file. This function returns the parsed ELF header.
		Elf_Ehdr* success = (Elf_Ehdr*) load_file(pcb_pointer, file_p);

		if (!success) {
			return (pcb*) -1;
		}

		//Debug, should be removed once scheduler works
		os_printf("THIS IS R13: %X \n", pcb_pointer->R13);

		//fill the free space with a pcb pointer
		*free_space_in_pcb_table = (uint32_t) pcb_pointer;
		//initialize PCB		
		pcb_pointer->PID = ++GLOBAL_PID;
		//4-13-15: function pointer should point to main() of file pointer.
		//         TODO: Eventually should be able to pass parameters. Put them on the stack (argv/argc)
		pcb_pointer->R15 = success->e_entry;

		// os_printf("%X ENTRY: %X \n", file_p, success->e_entry);

		pcb_pointer->current_state = PROCESS_NEW;

		pcb_pointer->has_executed = 0;
		return pcb_pointer;

	} else {
		os_printf("Out of memory in pcb table\n");
		return 0;
	}
}

//Cycles through pcb table and returns next free space
//If there is space, returns a pointer to the space
//returns 0 if no free space is available
uint32_t* process_next_free_slot_in_pcb_table() {
	uint32_t* current_address = pcb_table;
	uint32_t i;
	for (i = 0; i < MAX_PROCESSES; ++i) {
		if ((*(int*) current_address) == 0) {
			return current_address;
		}
		current_address += 1;
	}
	return 0;
}

//saves all the machine state of the process
//returns 0 for failure
//returns 1 for success
uint32_t process_save_state(uint32_t PID) {
	uint32_t* process_to_save = process_get_address_of_PCB(PID);
	pcb* pcb_p = process_get_PCB(PID);

	if (((uint32_t) process_to_save) == -1 || pcb_p == 0) {
		os_printf("Invalid PID in load_process_state");
		return 0;
	}

	asm("MOV %0, r0":"=r"(pcb_p->R0)::);
	asm("MOV %0, r1":"=r"(pcb_p->R1)::);
	asm("MOV %0, r2":"=r"(pcb_p->R2)::);
	asm("MOV %0, r3":"=r"(pcb_p->R3)::);
	asm("MOV %0, r4":"=r"(pcb_p->R4)::);
	asm("MOV %0, r5":"=r"(pcb_p->R5)::);
	asm("MOV %0, r6":"=r"(pcb_p->R6)::);
	asm("MOV %0, r7":"=r"(pcb_p->R7)::);
	asm("MOV %0, r8":"=r"(pcb_p->R8)::);
	asm("MOV %0, r9":"=r"(pcb_p->R9)::);
	asm("MOV %0, r10":"=r"(pcb_p->R10)::);
	asm("MOV %0, r11":"=r"(pcb_p->R11)::);
	asm("MOV %0, r12":"=r"(pcb_p->R12)::);
	asm("MOV %0, r13":"=r"(pcb_p->R13)::);
	asm("MOV %0, r14":"=r"(pcb_p->R14)::);
	asm("MOV %0, r15":"=r"(pcb_p->R15)::);

	return 1;

}

//R15 is the Program Counter
//R14 is the Link Register
//The last register to be loaded is the PC
//return 0 if fail
//return 1 for success
uint32_t process_load_state(uint32_t PID) {
	uint32_t* process_to_load = process_get_address_of_PCB(PID);
	pcb* pcb_p = process_get_PCB(PID);

	if (process_to_load == 0 || pcb_p == 0) {
		os_printf("Invalid PID in load_process_state");
		return 0;
	}

	asm("MOV r0, %0"::"r"(pcb_p->R0):);
	asm("MOV r1, %0"::"r"(pcb_p->R1):);
	asm("MOV r2, %0"::"r"(pcb_p->R2):);
	asm("MOV r3, %0"::"r"(pcb_p->R3):);
	asm("MOV r4, %0"::"r"(pcb_p->R4):);
	asm("MOV r5, %0"::"r"(pcb_p->R5):);
	asm("MOV r6, %0"::"r"(pcb_p->R6):);
	asm("MOV r7, %0"::"r"(pcb_p->R7):);
	asm("MOV r8, %0"::"r"(pcb_p->R8):);
	asm("MOV r9, %0"::"r"(pcb_p->R9):);
	asm("MOV r10, %0"::"r"(pcb_p->R10):);
	asm("MOV r12, %0"::"r"(pcb_p->R12):);
	asm("MOV r13, %0"::"r"(pcb_p->R13):);
	asm("MOV r14, %0"::"r"(pcb_p->R14):);
	asm("MOV r15, %0"::"r"(pcb_p->R15):);

	return 1;
}

uint32_t process_print_state(uint32_t PID) {
	pcb* pcb_p = process_get_PCB(PID);

	if (pcb_p == 0) {
		return 0;
	}

	os_printf("Process State of PID: %d\n", PID);
	os_printf("reg 0 = %x\n", pcb_p->R0);
	os_printf("reg 1 = %x\n", pcb_p->R1);
	os_printf("reg 2 = %x\n", pcb_p->R2);
	os_printf("reg 3 = %x\n", pcb_p->R3);
	os_printf("reg 4 = %x\n", pcb_p->R4);
	os_printf("reg 5 = %x\n", pcb_p->R5);
	os_printf("reg 6 = %x\n", pcb_p->R6);
	os_printf("reg 7 = %x\n", pcb_p->R7);
	os_printf("reg 8 = %x\n", pcb_p->R8);
	os_printf("reg 9 = %x\n", pcb_p->R9);
	os_printf("reg 10 = %x\n", pcb_p->R10);
	os_printf("reg 11 = %x\n", pcb_p->R11);
	os_printf("reg 12 = %x\n", pcb_p->R12);
	os_printf("reg 13 = %x\n", pcb_p->R13);
	os_printf("reg 14 = %x\n", pcb_p->R14);
	os_printf("reg 15 = %x\n", pcb_p->R15);

	return 1;
}

//destroys process with param PID by clearing the pcb struct
//returns 1 upon success, 0 with failure
uint32_t process_destroy(int PID) {
	uint32_t* addressToClear = process_get_address_of_PCB(PID);
	pcb* pcb_p = process_get_PCB(PID);
	uint32_t free_success = process_free_PCB(pcb_p);
	*addressToClear = 0; //clears the pointer to the PCB

	if (free_success) {
		return 1;
	} else {
		return 0;
	}

}

//prints the addresses of the pcbs stored in the table
void process_print_pcb_table() {
	os_printf("printing pcb table\n");
	uint32_t* current_address = (uint32_t*) pcb_table;
	uint32_t i;
	for (i = 0; i < MAX_PROCESSES; ++i) {
		os_printf("%x\n", current_address);
		current_address++;
	}
}

//Prints all of the PIDs in the pcb table
void process_print_PID() {

	uint32_t* current_address = pcb_table;
	uint32_t i;
	for (i = 0; i < MAX_PROCESSES; i++) {
		if ((*current_address) != 0) {
			// debug
			// os_printf("curr addr: %x\n", current_address);
			// os_printf("contents: %x\n", *current_address);

			pcb* temp_pcb = (pcb*) *current_address;
			os_printf("PID: %d\n", temp_pcb->PID);
		}
		current_address++;
	}
}

/* Returns a pointer to a pcb of process with @PID,
 or 0 if no process with PID exists.
 */
pcb* process_get_PCB(uint32_t PID) {

	//search for process in pcb table
	int i;
	uint32_t* current_address = pcb_table;

	for (i = 0; i < MAX_PROCESSES; ++i) {
		if ((*current_address) != 0) {
			pcb* pcb_p = (pcb*) *current_address;
			if (pcb_p->PID == PID) {
				return pcb_p;
			}
		}
		current_address++;
	}
	os_printf("Could not find process with PID: %d\n", PID);
	return 0;
}

/* returns a pointer to the address of a pcb in the table
 given the PID or if the PID is invalid
 */
uint32_t* process_get_address_of_PCB(uint32_t PID) {
	if (PID <= 0) {
		os_printf("Invalid PID of: %d, exiting.", PID);
		return 0;
	}

	int i;
	uint32_t* current_address = pcb_table;

	for (i = 0; i < MAX_PROCESSES; ++i) {
		if ((*current_address) != 0) {
			pcb* pcb_p = (pcb*) *current_address;
			if (pcb_p->PID == PID) {
				return current_address;
			}
		}
		current_address++;
	}

	os_printf("Did not find PID: %d\n", PID);
	return 0;
}

// this will 0 out everything in a PCB 
// accepts a pointer to a PCB
// returns 1 if successfully frees a pcb
uint32_t process_free_PCB(pcb* pcb_p) {
	if (pcb_p == 0) {
		os_printf("Can not free. Not a valid PCB.\n");
		return 0;
	}
	pcb_p->arg = 0;
	pcb_p->PID = 0;

	return 1;
}

/* executes a process function
 return PID upon success
 return 0 upon failure
 */
uint32_t process_execute(pcb* pcb_p) {

	if (!pcb_p) {
		os_printf("Cannot execute process. Exiting.\n");
		return 0;
	}

	//Copy the current process's program counter to the new process's return register
	//The new process will use R14 to return to the parent function
	asm("MOV %0, r15":"=r"(pcb_p->R14)::);

	//Switch to user virtual address space, this is self explanatory
//	vm_enable_vas(pcb_p->stored_vas);

	//Should be disabled once scheduler is working to prevent spam
	DEBUG("PID---->: %d\n", pcb_p->PID);
	DEBUG("Should be VAS: %x\n", vm_get_current_vas());

	//assert(1==2 && "process.c - We're stopping right after loading process state.");
	//4-15-15: Since execute_process is for new processes only, stored_vas must be empty 
	// assert(!pcb_p->stored_vas && "Assert error: trying to enter execute_process with already initialized process!");
	//4-13-15: Create new virtual address space for process and switch into it
	// Let's get a simple argc/argv layout going at 0x9f000000
	// Stick the program name at stack_base
//	vm_enable_vas(pcb_p->stored_vas);

//	print_process_state(pcb_p->PID);

	pcb_p->has_executed = 1;

	//Set state to running, this should be modified when the process is tossed into wait queues, etc
	//Check header file for a list of states
	pcb_p->current_state = PROCESS_RUNNING;

	//This will overwrite all our operating registers with the ones saved in the struct.
	//As soon as this is called the processor will start executing the new process.
	process_load_state(pcb_p->PID);
	while (1);
	return pcb_p->PID;
}

//executes a process function
//return 1 upon success
//return 0 upon failure
// uint32_t execute_process_no_vas(pcb* pcb_p) {
//     if(!pcb_p) {
//         os_printf("Cannot execute process. Exiting.\n");
//         return 0;
//     }
//     pcb_p->has_executed = 1;
//     pcb_p->function(pcb_p->PID);
//     return 1;
// }

//test function to see if execute process works correctly.
uint32_t sample_func(uint32_t x) {
	os_printf("Sample function!! From process with PID: %d\n", x);
	return 0;
}

void __process_init_vas(pcb* pcb_p);
void __process_init_heap(pcb* pcb_p);
void __process_init_stack(pcb* pcb_p);

void process_init(pcb * pcb_p) {
	__process_init_vas(pcb_p);
	__process_init_stack(pcb_p);
	__process_init_heap(pcb_p);
}

// Copy over from kernel to user space
void __process_init_vas(pcb* pcb_p) {
	for (int i = 0; i < 20; i++) {
		uint32_t *v = (uint32_t *)(pcb_p->start + (i * BLOCK_SIZE));
		vm_allocate_page(pcb_p->stored_vas, (void*) v, VM_PERM_USER_RW);
		vm_map_shared_memory(KERNEL_VAS, (void*) v, pcb_p->stored_vas,
				(void*) v, VM_PERM_USER_RW);

	}

	int *copyIn = pcb_p->start;
	int counter = 0;
	uint32_t * v = pcb_p->start;

	while (counter < pcb_p->len) {
		*v = *copyIn;
		copyIn += 1;
		v += 1;
		counter += 4;
	}

	for (int i = 0; i < 20; i++) {
		uint32_t *v = pcb_p->start + (i * BLOCK_SIZE);
		vm_free_mapping(KERNEL_VAS, (void*) v);
	}
}

//Initial page allocation for process stack in VAS
//Allows for a variety of stack limits
void __process_init_stack(pcb * pcb_p) {
	int retval = 0;

	for (int i = 0; i < (STACK_SIZE / BLOCK_SIZE); i++) {
		retval = vm_allocate_page(pcb_p->stored_vas,
				(void*) (STACK_BASE + (i * BLOCK_SIZE)), VM_PERM_USER_RW);
		if (retval) {
			ERROR("vm_allocate_page error code: %d\n", retval);
			break;
		}

		vm_map_shared_memory(KERNEL_VAS,
				(void*) (STACK_BASE + (i * BLOCK_SIZE)), pcb_p->stored_vas,
				(void*) (STACK_BASE + (i * BLOCK_SIZE)), VM_PERM_USER_RW);
	}

	// Stick a NULL at STACK_TOP-sizeof(int*)
	uint32_t *stack_top = (uint32_t*) STACK_TOP;
	stack_top[-1] = 0;
	stack_top[-2] = 0;
	stack_top[-3] = 0;
	stack_top[-4] = 0;
	stack_top[-5] = STACK_BASE;
	stack_top[-6] = 1;

	os_strcpy((char*)STACK_BASE, pcb_p->arg);

	// We need to set sp (r13) to stack_top - 12
	pcb_p->R13 = STACK_TOP - 4 * 6;
//	print_process_state(pcb_p->PID);

	for (int i = 0; i < (STACK_SIZE / BLOCK_SIZE); i++) {
		vm_free_mapping(KERNEL_VAS, (void*) (STACK_BASE + (i * BLOCK_SIZE)));
	}
}

void __process_init_heap(pcb* pcb_p) {
	//Initial page allocation for a process heap in VAS
	//process_print_state(pcb_p->PID);
	int retval = vm_allocate_page(pcb_p->stored_vas, (void*) HEAP_BASE,
	VM_PERM_USER_RW);
	if (retval) {
		ERROR("vm_allocate_page error code: %d\n", retval);
	}
	//assert(0 ==1 && "FUCK");
	//process_print_state(pcb_p->PID);
}
