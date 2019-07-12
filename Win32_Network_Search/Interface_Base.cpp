#include "Interface_Base.h"

/*
Interface_Base::Interface_Base(){
	printf("Interface_Base constructor\n");
}
Interface_Base::~Interface_Base(){
	printf("Interface_Base deconstructor\n");
}*/
int Interface_Base::Write(char *Out_Buffer)
{
	Write(Out_Buffer, strlen(Out_Buffer));
	return 1;
}