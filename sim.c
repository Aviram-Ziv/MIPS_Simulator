#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Memory_Size 3000
#define Disk_Sector_Size 512
#define Disk_Size 128*512
#define Disk_Number_of_Sectors 128
#define Monitor_Size 256
#define Disk_Time 1024 

void ValidateArguments(int argc, char* argv[]);
void LoadFiles(char* FileNames[], FILE* Files[]);
void SetupSystem(int Data_Memory[], int Disk[], int RegisterFile[], int IORegisters[], unsigned short Monitor[][Monitor_Size], FILE* Files[], int* pirq2_current);
void HandleInterrupt(int IORegisters[], FILE* Files[], int* clock, int* pPC, int* pirq2EOF, int* pirq2_current, int* pirq, int* pBranch_test);
void PrintTrace(FILE* Files[], int* pPC, int Cur_Instruction[], int RegisterFile[], char instruction_str[]);
void HandleDisk(int IORegisters[], int* pDmem_Secure, int* clock, int* pNDA, int Data_Memory[], int Disk[]);
void LoadInstructionData(int PC, FILE* fp_Imemin, int Cur_Instruction[], char buffer[]);
void ExecuteInstruction(int* pPC, int Cur_instruction[], int RegisterFile[], int Data_Memory[], int IORegisters[], FILE* Files[], int* clock, int* pDmem_Secure, unsigned short Monitor[][Monitor_Size], int* pirq, int* pBranch_test);
void CreateRemainingOutputFiles(FILE* Files[], int Data_Memory[], int Disk[], int RegisterFile[], int* clock, unsigned short Monitor[][Monitor_Size]);
void CloseFiles(FILE* Files[]);

int main(int argc, char* argv[])
{
	FILE* Files[14];
	int PC = 0; int* pPC = &PC; int Branch_test = 0; int* pBranch_test = &Branch_test;
	int Cur_Instruction[7]; //The instruction data will be stored separately by the different parts of the instruction. [0]-imm2, [1]-imm1, [2]-reserved, [3]-rt, [4]-rs, [5]-rd, [6]-opcode
	char instruction_str[20];
	int Data_Memory[Memory_Size]; int Dmem_Secure = Memory_Size; int* pDmem_Secure = &Dmem_Secure;
	int Disk[Disk_Number_of_Sectors * Disk_Sector_Size];
	int RegisterFile[16];
	int clock = 0, timer = 0; int* pclock = &clock;
	unsigned short Monitor[Monitor_Size][Monitor_Size];
	int IORegisters[23];
	int NDA = 0; int* pNDA = &NDA; //An integer that will hold the next clock cycle in which the disk will be free again, short for Next Disk Availability
	int irq2EOF = 0; int* pirq2EOF = &irq2EOF; int irq2_current = -1; int* pirq2_current = &irq2_current; //If irq2EOG = 1 then we know that we reached the end of irq2in.txt and no need to checl it 
	int irq = 0; int* pirq = &irq;

	ValidateArguments(argc, argv);
	LoadFiles(argv, Files);
	SetupSystem(Data_Memory, Disk, RegisterFile, IORegisters, Monitor, Files, pirq2_current);

	while (PC >= 0 && PC < Memory_Size)
	{
		LoadInstructionData(PC, Files[0], Cur_Instruction, instruction_str);
		PrintTrace(Files, pPC, Cur_Instruction, RegisterFile, instruction_str);
		ExecuteInstruction(pPC, Cur_Instruction, RegisterFile, Data_Memory, IORegisters, Files, pclock, pDmem_Secure, Monitor, pirq, pBranch_test);
		HandleDisk(IORegisters, pDmem_Secure, pclock, pNDA, Data_Memory, Disk);
		HandleInterrupt(IORegisters, Files, pclock, pPC, pirq2EOF, pirq2_current, pirq, pBranch_test);
		{
			if (*pirq == 1)
			{
				clock++;
				continue;
			}
		}
		if (Cur_Instruction[6] != 16 && (Cur_Instruction[6] < 7 || Cur_Instruction[6] > 13)) //Validating not to jump in case of branch, jal or reti (wherever PC is set within the function
			PC++;
		clock++;
	}

	CreateRemainingOutputFiles(Files, Data_Memory, Disk, RegisterFile, pclock, Monitor);
	CloseFiles(Files);

	return 0;
}

void ValidateArguments(int argc, char* arg[])
//	The purpose of ValidateArgument is to make sure that the program gets all the arguments that it 
//	expects, following the instructions.
//	The function will check if all the expected arguments are there.If so - it will allow continuing to the next stage, if  not - it will stop the program.
//	This will be done by checking that argc equals 16 (including the running file) and that the names of all the arguments  match the instructions.
//	Input: argc, argv
//	Output : an error message if there is no match, approval message otherwise
{
	int comparesult = 0, i = 0;
	//Initializing an array of strings that contains the names of all the expected arguments
	char arglist[14][20] = { "imemin.txt", "dmemin.txt", "diskin.txt", "irq2in.txt", "dmemout.txt", "regout.txt", "trace.txt", "hwregtrace.txt", "cycles.txt", "leds.txt", "display7seg.txt", "diskout.txt", "monitor.txt", "monitor.yuv" };

	if (argc != 15)
	{
		printf("The number of arguments is wrong\n");
		exit(-1);
	}
	for (i = 0; i < argc - 1; i++)
	{
		comparesult = strcmp(arglist[i], arg[i + 1]); //compare every argument to every string in the array 
		if (comparesult != 0)
		{
			printf("One or more of the arguments is incorrect\n");
			exit(-1);
		}
	}
	return;
}
void LoadFiles(char* FileNames[], FILE* Files[])
//The purpose of LoadFiles is to enable the simulator work by allowing it to read/write whatever it needs. 
//The function create an array of file pointers that point to the all the argumentsand have all the file open.
//Loadfile will get argvand an array of file type pointersand will open all the files in the right mode, depend on their role
//Input : argv[], address of the type file pointers array
//Output : None, this is a void function.In the end of this function, we will have an array of file pointers + all files are open in the right mode.
{
	int i = 0;

	//initalize pointers in Files array to be NULL;
	for (i = 0; i < 14; i++)
		Files[i] = NULL;

	//open all the input files
	for (i = 1; i < 5; i++)
	{
		fopen_s(&Files[i - 1], FileNames[i], "r");
		if (Files[i - 1] == NULL || Files[i - 1] == 00000000)
		{
			perror("There was an error opening the file");
			exit(-1);
		}
	}

	//open all the output files 
	for (i = 5; i < 15; i++)
	{
		fopen_s(&Files[i - 1], FileNames[i], "w");
		if (Files[i - 1] == NULL || Files[i - 1] == 00000000)
		{
			perror("There was an error opening the file");
			exit(-1);
		}
	}
	return;
}
void SetupSystem(int Data_Memory[], int Disk[], int RegisterFile[], int IORegisters[], unsigned short Monitor[][Monitor_Size], FILE* Files[], int* pirq2_current)
{
	//Setup Data memory - copy all data from dmemin.txt into an int array in the size of Memory_Size called Data_Memory

	int i = 0, j = 0, to_complete = 0, c = 0;
	char buffer[10];

	for (i = 0; i < Memory_Size; i++)
	{
		fseek(Files[1], i * 10, SEEK_SET); //find right line in dmemin.txt
		c = getc(Files[1]);
		if (c == EOF)
		{
			to_complete = Memory_Size - i;
			break;
		}
		fseek(Files[1], -1, SEEK_CUR);
		fgets(buffer, 10, Files[1]);
		sscanf_s(buffer, "%X", &Data_Memory[i]);
		//printf("This is line #%d in dmem and its value is: %.8X:\n", i, Data_Memory[i]); //testing
	}
	for (i = Memory_Size - to_complete; i < Memory_Size; i++) //Initialize rest of values to 0 (in case not all possible lines of dmem are given
		Data_Memory[i] = 0;

	//Setup Disk - copy all data from diskin.txt into an int array in the size of Disk_Size

	for (i = 0; i < Disk_Size; i++)
	{
		fseek(Files[2], i * 10, SEEK_SET);
		c = getc(Files[2]);
		if (c == EOF)
		{
			to_complete = Disk_Size - i;
			break;
		}
		fseek(Files[2], -1, SEEK_CUR);
		fgets(buffer, 10, Files[2]);
		sscanf_s(buffer, "%X", &Disk[i]);
	}
	for (i = Disk_Size - to_complete; i < Disk_Size; i++)
	{
		Disk[i] = 0;
		//printf("This is line #%d in disk and its value is: %.8X:\n", i, Disk[i]); //testing
	}

	//Initialize RegisterFile to 0
	for (i = 0; i < 16; i++)
	{
		RegisterFile[i] = 0;
		//printf("Register #%d is initialized to be: %.8X\n", i, i); //testing
	}

	//Initialize IORegsiters to 0
	for (i = 0; i < 23; i++)
	{
		IORegisters[i] = 0;
		//printf("IO Register #%d is initialized to be: %.8X\n", i, i); //testing
	}

	//initialize Monitor 
	for (i = 0; i < Monitor_Size; i++)
	{
		for (j = 0; j < Monitor_Size; j++)
		{
			Monitor[i][j] = 0;
			//printf("%d ", Monitor[i][j]); //testing
		}
		//printf("\n");
	}

	//initialize irq2's first value 
	fseek(Files[3], 0, SEEK_SET);
	fgets(buffer, 10, Files[3]); //Copy value from irq2in as a strings 
	*pirq2_current = atoi(buffer);

	return;
}
void PrintTrace(FILE* Files[], int* pPC, int Cur_Instruction[], int RegisterFile[], char instruction_str[])
{
	int i = 0;

	fprintf(Files[6], "%.3X ", *pPC);
	fprintf(Files[6], "%s ", instruction_str);
	fprintf(Files[6], "00000000 ");
	fprintf(Files[6], "%.8x %.8x ", (signed int)Cur_Instruction[1], (signed int)Cur_Instruction[0]);
	for (i = 3; i < 15; i++)
		fprintf(Files[6], "%.8x ", RegisterFile[i]);
	fprintf(Files[6], "%.8x\n", RegisterFile[15]);
	return;
}
void HandleDisk(int IORegisters[], int* pDmem_Secure, int* clock, int* pNDA, int Data_Memory[], int Disk[])
{
	//In this function we manage the entire operation of the disk: (1) First we check if the disk got to the clock cycle in which it needs new configuration, (2) In case this is not the case - we see if it's busy. If so - we return. (3) 
	if (*clock == *pNDA) //We check if we reached the clock cycle in which 1024 (Disk_Time) clock cycles has passed since our last write/read command 
	{
		IORegisters[14] = 0; //Setting diskcmd to be 0
		IORegisters[17] = 0; //Setting diskstatus to be 0
		IORegisters[4] = 1; //Setting irq1status to be 1
		*pDmem_Secure = Memory_Size; //Here we make sure that Dmem_Secure is not pointing for any valid address in Dmem, thus eliminiating its relevance in the check we do in the instruction execution 
		return; //Now the disk is availble, every new handling will occur in the next cycle 
	}
	if (IORegisters[17] == 1) //We check if diskstatus register is 1 - if so, then the disk is busy and there's nothing to do with it
	{
		return;
	}
	else if (IORegisters[17] == 0) //The disk is available
	{
		//Below: validating that: (1) There is a command for writing to the disk or reading from it, (2) disksector and diskbuffer are initialized to a valid values (both values are initialized to 0 after seting up the system). We assume that the user/assembly code initialized the desired values but in any case the read/write operation will work)
		if ((IORegisters[14] == 1 || IORegisters[14] == 2) && (0 <= IORegisters[16] && IORegisters[16] <= Memory_Size - 1) && (0 <= IORegisters[15] && IORegisters[15] <= Disk_Sector_Size - 1))
		{
			*pDmem_Secure = IORegisters[16]; //We hold the address in the memory in which we will start writing/reading 512 bytes = 128 words and we will use this address to verify that sw and lw commands do not overwrite what we are copying or writing
			*pNDA = *clock + Disk_Time + 1; //pNDA will store the value of the next clock cycle in which the disk will terminate its operation and will be available again  
			int i = 0;
			if (IORegisters[14] == 1) //In case diskcmd = 1 and we want to read from disk 
			{
				for (i = 0; i < Disk_Sector_Size; i++) //Copying all the data from the relevant sector to the relevant address in Dmem
					Data_Memory[IORegisters[16] + i] = Disk[((IORegisters[15] - 1) * Disk_Sector_Size) + i];
			}
			else //Since we have already validated that diskcmd = 1/2, the option that is left is to write to the disk 
			{
				for (i = 0; i < Disk_Sector_Size; i++) //Copying all the data from the relevant address in dmem to the relevant address in the disk
					Disk[((IORegisters[15] - 1) * Disk_Sector_Size) + i] = Data_Memory[IORegisters[16] + i];
			}
		}
	}
	else
	{
		printf("Invalid value for diskstatus");
		exit(-1);
	}
	return;
}
void HandleInterrupt(int IORegisters[], FILE* Files[], int* clock, int* pPC, int* pirq2EOF, int* pirq2_current, int* pirq, int* pBranch_test)
{
	int irq = 0;

	if (*pirq == 1)
	{
		*pPC = *pPC + 1;
		return;
	}

	//Check timer - if irq0status should be 1

	if (IORegisters[11] == 1 && IORegisters[12] < IORegisters[13]) //Here we check to see if timerenable == 1 and if timercurrent is smaller than timermax. If both terms hold, then we increment timercurrent by 1
	{
		IORegisters[12]++;
		printf("timer updated\n"); //testing
	}
	else if (IORegisters[11] == 1 && IORegisters[12] == IORegisters[13]) //In case the timer is enabled and timercurrent == timermax -> we zero timercurrent out and turn on irq0status to 1
	{
		IORegisters[3] = 1;
		IORegisters[12] = 0;
		printf("irq0status = %d, timercurrent = %d\n", IORegisters[3], IORegisters[12]);
	}

	//Check disk  - irq1status is handled in HandleDisk function

	//Check irq2 - if irq2status should be 1. This will be done as follows - in every clock cycle we will check the number in irq12.txt: if it equals the clock then we will turn irq2status on, otherwise we will continue 

	char buffer[20]; //Assuming that the maximal irq2 value will not be larger than 20 digits 
	char c; //This will help us check if we have reached the end of the file 
	int length = 0; //We will use this variable to know how many places to jump ahead within the irq12.txt in order to go to the next instance 

	if (*pirq2EOF == 0) //If we haven't reached the end of the file 
	{
		if (*pirq2_current == *clock)
		{
			IORegisters[5] = 1; //Setting irq2status to be 1
			c = getc(Files[3]);
			if (c == EOF) //Checking if we have reached the end of the file 
			{
				printf("End of irq2 in clock cycle %d\n", *clock);
				IORegisters[5] = 0; //Setting irq2status to be 0 -> We reached the end of the file
				*pirq2EOF = 1; //making sure there will be no more reading from this file
			}
			else //Read next irq2 clock cycle
			{
				fseek(Files[3], -1, SEEK_CUR); //Go 1 charachter back (getc promoted us in 1 charachter)
				fgets(buffer, 10, Files[3]); //Copy value from irq2in as a strings 
				*pirq2_current = atoi(buffer);
			}
		}
	}

	irq = (IORegisters[0] && IORegisters[3]) || (IORegisters[1] && IORegisters[4]) || (IORegisters[2] && IORegisters[5]);

	if (irq == 0) //There is no interruption 
		return;
	else
	{
		*pirq = 1; //Letting the system now there's an interrupt
		if (*pBranch_test == 1)
			IORegisters[7] = *pPC - 1; //In case of  taken branch
		else
			IORegisters[7] = *pPC;// irqreturn = PC
		*pPC = IORegisters[6]; //PC = irqhandler
		return;
	}
}
void LoadInstructionData(int PC, FILE* fp_Imemin, int Cur_Instruction[], char buffer[])
//The purpose of the function is to tell the simulator what to do - it gets the next instruction and break it to the different elements of each instruction.
//The function will go to the PCth line in the Imem file and will copy the content of the instruction there [12 charachters] to a buffer string.
//The function will break the instruction string to the different parts of the instruction, and for each part, using sscan_f(), it will retrieve a formatted output - integer type in this case, which will represent the value of every part in the instruction. These values will be stored in an array of integers which contains all the data of the instruction and will be used later in the execution
//Input: PC, file pointer to Imem, pointer to the instruction array (the instruction is an array where each entry is for different part of the instruction)
//Output : This is a void function so no output, just updating the instruction array
{
	char divided_instruction[7][6];
	int i = 0, j = 0, string_length = 0;

	fseek(fp_Imemin, PC * 14, SEEK_SET); //Jump to the PCth line in Imemin.txt
	fgets(buffer, 13, fp_Imemin); //Copy the 12 charachters of the instruction to the buffer

	//We assume that the input is valid, as said in the instructions, meaning - each instruction is 12 hex digits long

	//Breaking down the imm2 part
	divided_instruction[0][0] = buffer[9];
	divided_instruction[0][1] = buffer[10];
	divided_instruction[0][2] = buffer[11];

	//Breaking down the imm1 part
	divided_instruction[1][0] = buffer[6];
	divided_instruction[1][1] = buffer[7];
	divided_instruction[1][2] = buffer[8];

	//Breaking down the reserved part
	divided_instruction[2][0] = buffer[5];

	//Breaking down the rt part
	divided_instruction[3][0] = buffer[4];

	//Breaking down the rs part
	divided_instruction[4][0] = buffer[3];

	//Breaking down the rd part
	divided_instruction[5][0] = buffer[2];

	//Breaking down the opcode part
	divided_instruction[6][0] = buffer[0];
	divided_instruction[6][1] = buffer[1];

	//At this point all the different parts of the instruction (which is given as a string) are broken into separtate strings so we can use sscan_f to convert them to integer type with the for loop below 
	for (i = 0; i < 7; i++)
	{
		sscanf_s(divided_instruction[i], "%X", &Cur_Instruction[i]);
	}
	//Sign extending imm1 and imm2 
	int sign_tester = -1;
	if (sign_tester = 0x800 & Cur_Instruction[0]) //Checking if sign bit = 1
		Cur_Instruction[0] = 0xFFFFF000 | Cur_Instruction[0];
	if (sign_tester = 0x800 & Cur_Instruction[1]) //Checking if sign bit = 1
		Cur_Instruction[1] = 0xFFFFF000 | Cur_Instruction[1];
	return;
}
void ExecuteInstruction(int* pPC, int Cur_instruction[], int RegisterFile[], int Data_Memory[], int IORegisters[], FILE* Files[], int* clock, int* pDmem_Secure, unsigned short Monitor[][Monitor_Size], int* pirq, int* pBranch_test)
{
	int rs = 0, rt = 0, rd = 0, imm1 = 0, imm2 = 0;

	*pBranch_test = 0; //Initialize a variable that will tell us if a branch was taken in this clock cycle. If so, we will use it in the HandleInterrupt function by setting irqreturn to be the new PC - 1 because reti increments PC by 1 and if there was a branch we would want to get to the address given exactly

	int initial_PC = *pPC;

	rd = Cur_instruction[5];
	rs = Cur_instruction[4];
	rt = Cur_instruction[3];
	imm1 = Cur_instruction[1];
	imm2 = Cur_instruction[0];
	char IORegName[50];
	int IORegNum = 0;
	RegisterFile[0] = 0; RegisterFile[1] = imm1; RegisterFile[2] = imm2; //Making sure R1-3's values hold in any case 

	if (Cur_instruction[6] == 17 || Cur_instruction[6] == 18)
	{
		IORegNum = RegisterFile[rs] + RegisterFile[rt];
		switch (IORegNum)
		{
		case 0:
			strcpy_s(IORegName, 50, "irq0enable");
			break;
		case 1:
			strcpy_s(IORegName, 50, "irq1enable");
			break;
		case 2:
			strcpy_s(IORegName, 50, "irq2enable");
			break;
		case 3:
			strcpy_s(IORegName, 50, "irq0status");
			break;
		case 4:
			strcpy_s(IORegName, 50, "irq1status");
			break;
		case 5:
			strcpy_s(IORegName, 50, "irq2status");
			break;
		case 6:
			strcpy_s(IORegName, 50, "irqhandler");
			break;
		case 7:
			strcpy_s(IORegName, 50, "irqreturn");
			break;
		case 8:
			strcpy_s(IORegName, 50, "clks");
			break;
		case 9:
			strcpy_s(IORegName, 50, "leds");
			break;
		case 10:
			strcpy_s(IORegName, 50, "display7seg");
			break;
		case 11:
			strcpy_s(IORegName, 50, "timerenable");
			break;
		case 12:
			strcpy_s(IORegName, 50, "timercurrent");
			break;
		case 13:
			strcpy_s(IORegName, 50, "timermax");
			break;
		case 14:
			strcpy_s(IORegName, 50, "diskcmd");
			break;
		case 15:
			strcpy_s(IORegName, 50, "disksector");
			break;
		case 16:
			strcpy_s(IORegName, 50, "diskbuffer");
			break;
		case 17:
			strcpy_s(IORegName, 50, "diskstatus");
			break;
		case 18:
			strcpy_s(IORegName, 50, "reserved");
			break;
		case 19:
			strcpy_s(IORegName, 50, "reserved");
			break;
		case 20:
			strcpy_s(IORegName, 50, "monitoraddr");
			break;
		case 21:
			strcpy_s(IORegName, 50, "monitordata");
			break;
		case 22:
			strcpy_s(IORegName, 50, "monitorcmd");
			break;
		default:
			printf("Invalid IO register number\n");
		}
	}
	switch (Cur_instruction[6])
	{
	case 0: //add
		RegisterFile[rd] = RegisterFile[rs] + RegisterFile[rt];
		break;
	case 1: //sub
		RegisterFile[rd] = RegisterFile[rs] - RegisterFile[rt];
		break;
	case 2: //and
		RegisterFile[rd] = RegisterFile[rs] & RegisterFile[rt];
		break;
	case 3: //or
		RegisterFile[rd] = RegisterFile[rs] | RegisterFile[rt];
		break;
	case 4: //sll
		RegisterFile[rd] = RegisterFile[rs] << RegisterFile[rt];
		break;
	case 5: //sra
		RegisterFile[rd] = RegisterFile[rs] >> RegisterFile[rt];
		if (RegisterFile[rs] < 0)
			RegisterFile[rd] = RegisterFile[rd] | 0x80000000; //Making sure that the MSB is 1	
		break;
	case 6: //srl
		RegisterFile[rd] = RegisterFile[rs] >> RegisterFile[rt];
		break;
	case 7: //beq
		if (RegisterFile[rs] == RegisterFile[rt])
			*pPC = RegisterFile[rd] & 0x00000FFF;
		else
			*pPC = *pPC + 1;
		break;
	case 8: //bne
		if (RegisterFile[rs] != RegisterFile[rt])
			*pPC = RegisterFile[rd] & 0x00000FFF;
		else
			*pPC = *pPC + 1;
		break;
	case 9: //blt
		if (RegisterFile[rs] < RegisterFile[rt])
			*pPC = RegisterFile[rd] & 0x00000FFF;
		else
			*pPC = *pPC + 1;
		break;
	case 10: //bgt
		if (RegisterFile[rs] > RegisterFile[rt])
			*pPC = RegisterFile[rd] & 0x00000FFF;
		else
			*pPC = *pPC + 1;
		break;
	case 11: //ble
		if (RegisterFile[rs] <= RegisterFile[rt])
			*pPC = RegisterFile[rd] & 0x00000FFF;
		else
			*pPC = *pPC + 1;
		break;
	case 12: //bge
		if (RegisterFile[rs] >= RegisterFile[rt])
			*pPC = RegisterFile[rd] & 0x00000FFF;
		else
			*pPC = *pPC + 1;
		break;
	case 13: //jal
		RegisterFile[15] = *pPC + 1;
		*pPC = imm2;
		break;
	case 14: //lw
		//Here we check if there is a load word command while the writing/reading to/from dmem and the disk is still going on. We first check it the disk is busy - if so, *pDmem_Secure will hold the value of the memory address that is busy and will take a buffer of 512 bytes = 128 words to ensure that no data is read or written unsafely
		if (((RegisterFile[rs] + RegisterFile[rt]) > *pDmem_Secure && (RegisterFile[rs] + RegisterFile[rt]) < (*pDmem_Secure + (Disk_Sector_Size - 1))) && IORegisters[17] == 1)
		{
			printf("In clock cycle #%d there has been a trial to read from a busy memory", *clock);
			break;
		}
		if ((RegisterFile[rs] + RegisterFile[rt] >= Memory_Size))
		{
			printf("The address you seek is beyond the valid addresses, the program will close now");
			exit(-1);
		}
		RegisterFile[rd] = Data_Memory[RegisterFile[rs] + RegisterFile[rt]];
		break;
	case 15: //sw
		//The same but just in the case of storing a word in dmem
		if (((RegisterFile[rs] + RegisterFile[rt]) > *pDmem_Secure && (RegisterFile[rs] + RegisterFile[rt]) < (*pDmem_Secure + (Disk_Sector_Size - 1))) && IORegisters[17] == 1)
		{
			printf("In clock cycle #%d there has been a trial to write to a busy memory", *clock);
			exit(-1);
			break;
		}
		if ((RegisterFile[rs] + RegisterFile[rt] >= Memory_Size))
		{
			printf("The address you seek is beyond the valid addresses, the program will close now");
			exit(-1);
		}
		Data_Memory[RegisterFile[rs] + RegisterFile[rt]] = RegisterFile[rd];
		if (RegisterFile[rd] == 0xF)
		{
			printf("Error in clock cycle #%d, value in R[rd] is: %d\n", *clock, RegisterFile[rd]);
			exit(-1);
		}
		break;
	case 16: //reti
	{
		*pPC = IORegisters[7] + 1;
		*pirq = 0;
		break;
	}
	case 17: //in
		fprintf(Files[7], "%d READ %s %.8x\n", *clock, IORegName, IORegisters[RegisterFile[rs] + RegisterFile[rt]]); //Updating hwregtrace.txt
		RegisterFile[rd] = IORegisters[RegisterFile[rs] + RegisterFile[rt]];
		if (RegisterFile[rs] + RegisterFile[rt] == 22) //Reading from monitor CMD
			RegisterFile[rd] = 0;
		break;
	case 18: //out
	{
		IORegisters[RegisterFile[rs] + RegisterFile[rt]] = RegisterFile[rd];
		if (RegisterFile[rs] + RegisterFile[rt] == 9) //Checking if we write to leds. If so - we shall update leds.txt
			fprintf(Files[9], "%d %.8x\n", *clock, RegisterFile[rd]); //We treat the leds status after this instruction
		if (RegisterFile[rs] + RegisterFile[rt] == 10) //Checking if we write to 7segmentdisplay. If so - we shall update display7seg.txt
			fprintf(Files[10], "%d %.8x\n", *clock, RegisterFile[rd]); //We treat the display status after this instruction
		if (RegisterFile[rs] + RegisterFile[rt] == 22 && RegisterFile[rd] == 1) //Checking if we are writing to monitorcmd and if so - then if we write to it -> 1 = write pixel to monitor 
		{
			int monitor_line = 0, monitor_column = 0;
			monitor_line = IORegisters[20] / Monitor_Size;
			monitor_column = IORegisters[20] % Monitor_Size;
			Monitor[monitor_line][monitor_column] = IORegisters[21];
		}
		fprintf(Files[7], "%d WRITE %s %.8x\n", *clock, IORegName, RegisterFile[rd]); //Updating hwregtrace.txt
	}
	break;
	case 19: //halt
		*pPC = -2;
		return;
	default:
		printf("This is not a valid instruction");
		exit(-1);
	}
	if (initial_PC != *pPC && Cur_instruction[6] != 16)
		*pBranch_test = 1;
	RegisterFile[0] = 0; RegisterFile[1] = imm1; RegisterFile[2] = imm2; //Making sure R1-3's values hold in any case 
	return;
}
void CreateRemainingOutputFiles(FILE* Files[], int Data_Memory[], int Disk[], int RegisterFile[], int* clock, unsigned short Monitor[][Monitor_Size])
//This function creates all the output files that are not being updated in every loop such as trace.txt
//The output files that will be created in this function are: dmemout.txt, regout.txt, cycles.txt, diskout.txt, monitor.txt 
{
	int i = 0, j = 0;
	//Creating dmemout.txt
	for (i = 0; i < Memory_Size; i++)
	{
		fprintf(Files[4], "%.8X\n", Data_Memory[i]);
	}

	//Creating diskout.txt
	for (i = 0; i < Disk_Size; i++)
	{
		fprintf(Files[11], "%.8X\n", Disk[i]);
	}

	//Creating regout.txt
	for (i = 3; i < 16; i++)
		fprintf(Files[5], "%.8X\n", RegisterFile[i]);

	//Creating cycles.txt
	fprintf(Files[8], "%d", *clock);

	//Creating monitor.txt
	for (i = 0; i < Monitor_Size; i++)
	{
		for (j = 0; j < Monitor_Size; j++)
		{
			fprintf(Files[12], "%.2X\n", Monitor[i][j]);
		}
	}
}
void CloseFiles(FILE* Files[])
//The purpose of this function is to make sure that at the end of the run we close all the files. 
//The function simply closes the argument files.
//It does it by running over the file pointer array and closing every file in.
//Input: Files array, which is the array the holds all the file pointers to the arguments.
//Output: Nothing, this is a void function. For the protocol, it prints to the screen that the files are closed
{
	int i = 0;

	for (i = 0; i < 14; i++)
		fclose(Files[i]);

	return;
}