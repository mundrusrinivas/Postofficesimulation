// PostOfficeSimulation.cpp : Defines the entry point for the console application.
//
#include <stdio.h>
#include <stdlib.h>
#include <deque>
#include <string>

#include <boost/thread.hpp>
#include <boost/thread/scoped_thread.hpp>
#include <boost/chrono.hpp>
using namespace std;

#define MAX_STRING 128

//I am using STL queue we can use raw linked list as a queue 
struct CUSTOMERDATA
{
	long SerialNumber;
	char CustomerName[MAX_STRING];
	bool IsServed; 
	bool IsSavingsCustomer;
};

FILE *g_fp = NULL;

//Global Serial NUMBER
long g_SerialNumber = 0;
bool g_bShutdown = false; // TO indicate Threads to stop
int g_NoOfLineworkers = 0; // TO create threads
//Seq Number to calculate Insert operations
long g_TotalInserts = 0;
long g_InsertsNeededForCopy = 0; //How many inserts needed for copy

//Global Critical Section to handle MultiThread scneario
boost::recursive_mutex g_customerqueueguard;
boost::recursive_mutex g_servicequeueguard;
boost::recursive_mutex g_savingsqueueguard;
boost::recursive_mutex g_backupqueueguard;
boost::recursive_mutex g_logguard;

//4 Queues 
deque<CUSTOMERDATA *> g_CustomersQueue;          //To Hold Customer Data
deque<CUSTOMERDATA *> g_SavingsCustomerQueue;    //To Hold Priority Saving account customers
deque<CUSTOMERDATA *> g_ServiceQueue;    //To Hold service Finished Customers to Maintain Data for reference
deque<CUSTOMERDATA *> g_BackUpQueue;             //To Hold BackUP queues if primary queue is full

void WritetoLog(char *data)
{
	boost::lock_guard<boost::recursive_mutex> lock(g_logguard);
	g_fp = fopen("Output.log", "a+");
	fprintf(g_fp, "%s\n", data);
	fclose(g_fp);
}

void AddCustomerToBackUPQueue(CUSTOMERDATA* customerData, bool bCreateMemory = true)
{
	char sz[MAX_STRING];
	{
		boost::lock_guard<boost::recursive_mutex> lock(g_backupqueueguard);
		CUSTOMERDATA *tmpqueueData = customerData;
		if (bCreateMemory)
		{
			tmpqueueData = new CUSTOMERDATA();
			memcpy(tmpqueueData, customerData, sizeof(CUSTOMERDATA));
		}
		g_BackUpQueue.push_back(tmpqueueData);
		sprintf(sz, "Pushed data to backup queue : %d", tmpqueueData->SerialNumber);
		WritetoLog(sz);
	}
}


void AddCustomerToNormalQueue(CUSTOMERDATA* customerData)
{
	{
		boost::lock_guard<boost::recursive_mutex> lock(g_customerqueueguard);
		g_CustomersQueue.push_back(customerData);
	}
}

void AddCustomerToSavingsQueue(CUSTOMERDATA* customerData)
{
	{
		boost::lock_guard<boost::recursive_mutex> lock(g_savingsqueueguard);
		g_SavingsCustomerQueue.push_back(customerData);
	}
}

void AddCustomerToServiceQueue(CUSTOMERDATA* customerData)
{
	{
		boost::lock_guard<boost::recursive_mutex> lock(g_servicequeueguard);
		g_ServiceQueue.push_back(customerData);
	}
}

void AddCustomerToQueue(CUSTOMERDATA* customerData)
{
	if (customerData != NULL)
	{
		if (customerData->IsSavingsCustomer)
			AddCustomerToSavingsQueue(customerData);
		else
			AddCustomerToNormalQueue(customerData);

		g_TotalInserts++; //Increment TotalInserts

		if (g_TotalInserts >= g_InsertsNeededForCopy && g_InsertsNeededForCopy > 0)
		{
			for (deque<CUSTOMERDATA *>::iterator itr = g_ServiceQueue.begin(); itr != g_ServiceQueue.end(); itr++)
			{
				AddCustomerToBackUPQueue(*itr);
			}
			for (deque<CUSTOMERDATA *>::iterator itr = g_SavingsCustomerQueue.begin(); itr != g_SavingsCustomerQueue.end(); itr++)
			{
				AddCustomerToBackUPQueue(*itr);
			}
			for (deque<CUSTOMERDATA *>::iterator itr = g_CustomersQueue.begin(); itr != g_CustomersQueue.end(); itr++)
			{
				AddCustomerToBackUPQueue(*itr);
			}
			g_TotalInserts = 0; //Reset Counter to 0
		}
	}
}

CUSTOMERDATA* GetCustomerForSavingsQueue()
{
	char sz[MAX_STRING];
	CUSTOMERDATA* customerData = NULL;
	{
		boost::lock_guard<boost::recursive_mutex> lock(g_savingsqueueguard);
		if (g_SavingsCustomerQueue.size() > 0)
		{
			customerData = g_SavingsCustomerQueue.front();
			//Add the customer to ServiceQueue
			g_SavingsCustomerQueue.pop_front();
			sprintf(sz, "Processing Savings Customer : %d", customerData->SerialNumber);
			WritetoLog(sz);
		}
	}
	return customerData;
}

CUSTOMERDATA* GetCustomerForNormalQueue()
{
	char sz[MAX_STRING];
	CUSTOMERDATA* customerData = NULL;
	{
		boost::lock_guard<boost::recursive_mutex> lock(g_customerqueueguard);
		if (g_CustomersQueue.size() > 0)
		{
			customerData = g_CustomersQueue.front();
			g_CustomersQueue.pop_front();
			sprintf(sz, "Processing Normal Customer : %d", customerData->SerialNumber);
			WritetoLog(sz);
		}
	}
	return customerData;
}

CUSTOMERDATA* GetCustomerForService()
{
	CUSTOMERDATA* customerData = GetCustomerForSavingsQueue();
	if (customerData == NULL)
		customerData = GetCustomerForNormalQueue();
	return customerData;
}

long GetNextSerialNumber()
{
	return ++g_SerialNumber;
}

//This is thread function simulates Post Office Line Work
void LineWorkerWork()
{
	char sz[MAX_STRING];
	//Check ANY Priority Customer who have Saving Accounts always
	while (!g_bShutdown)
	{
		CUSTOMERDATA * customerData = GetCustomerForService();
		//Wait for 10 milliseconds and try again
		if (customerData == NULL)
		{
			//Wait for Some time and Try again
			boost::posix_time::milliseconds(10);
		}
		else
		{
			//Simulate the process by Sleeping for 100 seconds
			customerData->IsServed = true;
			boost::posix_time::milliseconds(100000); //
			
			if (customerData->IsSavingsCustomer)
				sprintf(sz, "Completed processing Savings Customer: %d", customerData->SerialNumber);
			else
				sprintf(sz, "Completed processing Normal Customer: %d", customerData->SerialNumber);
			WritetoLog(sz);
			//This is processed so adding to service queue
			AddCustomerToServiceQueue(customerData);

		}
	}
}

void AddCustomer( int IsSavingsUser, char *CustomerName = NULL)
{
	CUSTOMERDATA *customerData = new CUSTOMERDATA();
	if( CustomerName != NULL )
		strcpy(customerData->CustomerName, CustomerName);
	customerData->IsServed = false;
	customerData->IsSavingsCustomer = IsSavingsUser > 0 ? true: false;
	customerData->SerialNumber = GetNextSerialNumber();

	AddCustomerToQueue(customerData);
}

void DeleteDataFromQueue(deque<CUSTOMERDATA*> queue)
{
	while (queue.size())
	{
		CUSTOMERDATA * data = queue.front();
		if (data)
		{
			delete data;
			data = NULL;
		}
		queue.pop_front();
	}
}


int main()
{
	int choice, Value;
	printf("Please enter No of line of workers working in PostOffice and Upfront Customer \n");
	scanf("%d", &g_NoOfLineworkers);
	if (g_NoOfLineworkers <= 0)
	{
		printf("Please enter valid number for line of workers working in PostOffice \n");
		exit(0);
	}
	
	for (int i = 0; i < g_NoOfLineworkers; i++)
	{
		//Creating Threads
		boost::thread workerThread(LineWorkerWork);
	}

	int NormalQueueMembers = 0, SavingsQueueMembers = 0;
	printf("Please enter Initial Normal Customers, if not enter 0\n");
	scanf("%d", &NormalQueueMembers);

	
	printf("Please enter Initial Savings Customers, if not enter 0\n");
	scanf("%d", &SavingsQueueMembers);

	if (NormalQueueMembers > 0)
		for (int i = 0; i < NormalQueueMembers; i++)
			AddCustomer(0);

	if (SavingsQueueMembers > 0)
		for (int i = 0; i < SavingsQueueMembers; i++)
			AddCustomer(1);

	printf("Please enter Backup queue number, if not enter 0\n");
	scanf("%d", &g_InsertsNeededForCopy);
	
	while (!g_bShutdown)
	{
		printf("Please enter below choice \n");
		printf("1: To Add Normal Customer \n");
		printf("2: To Add Savings Customer \n");
		printf("3: To Know how many customers in queue \n");
		printf("4: To Know how many customers serviced today  \n");
		printf("5: To Know How many customers came to postoffice in total \n");
		printf("6: To Know How many entries there in backup queue \n");
		printf("7: Exit \n");

		scanf("%d", &Value);
		switch (Value)
		{
			case 1:
				AddCustomer(0);
				printf("Added Customer Successfully; His Serial Number is %d \n", g_SerialNumber);
				break;
			case 2:
				AddCustomer(1);
				printf("Added Customer Successfully; His Serial Number is %d \n", g_SerialNumber);
				break;
			case 3:
				printf("Normal Customers: %d, Saving Customers: %d \n", g_CustomersQueue.size(), g_SavingsCustomerQueue.size());
				break;
			case 4:
				printf("Customer Serviced: %d \n", g_ServiceQueue.size());
				break;
			case 5:
				printf("Today's Total Customers: %d \n", g_SerialNumber);
				break;
			case 6:
				printf("Backup QueueCount: %d \n", g_BackUpQueue.size());
				break;
			case 7:
				g_bShutdown = true;
				//Wait for 10 secs
				boost::posix_time::milliseconds(10000); 
				break;
		}
	}
	DeleteDataFromQueue(g_BackUpQueue);
	DeleteDataFromQueue(g_ServiceQueue);
	DeleteDataFromQueue(g_SavingsCustomerQueue);
	DeleteDataFromQueue(g_CustomersQueue);
    return 0;
}

