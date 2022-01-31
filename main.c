#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <pthread.h>

#define SHARED_SIZE 10
#define INPUT_SIZE 100
#define KONIEC 2
#define DALEJ 1

/*-------------------------semafor do poprawnego wypisywania----------------*/
sem_t *wypisywanie;
sem_t *dostep;
int sem;

/*-------------------------Buffer do kolejki p1->p2-------------------------*/
struct message_buffer {
	long msg_type;
	char msg_text[100];
};

/*-------------------------Buffer do kolejki p2->p3-------------------------*/
struct message2_buffer {
	long msg_type;
	int msg_text;
};


struct sigaction PactionM;
struct sigaction Paction1;
struct sigaction Paction2;
struct sigaction Paction3;
struct sigaction PactionD;

/* Do kopiowania do shmem. */
int *sh_mem_pid1;
int *sh_mem_pid2;
int *sh_mem_pid3;
int *sh_mem_signal;

sigset_t mask1;
sigset_t mask2;
sigset_t mask3;

int signallll;

//info->si_pid
void p3_signal_capture(int sig, siginfo_t *info, void *ucontext) {
  	if (sig == SIGTSTP || sig == SIGCONT || sig == SIGTERM) { //proces P3 sprawdza czy odebrany sygnal jest jednym z 3 sygnalow operowanych przez program
        	kill(getppid(), sig); //P3 wysyla sygnal do PM
        	return;
      	}
  	if (sig == SIGUSR1) { //sytuacja, w ktorej sygnal odebrany przez P3 od uzytkownika zatacza kolo i wraca z powrotem do P3
        	sem_wait(dostep); //opuszczamy semafor
        	if (info->si_pid == *sh_mem_pid2) { //proces P3 upewnia sie czy dostal sygnal od P2
              	sig = *sh_mem_signal; //P3 odbiera sygnal z pamieci wspoldzielonej
              	sem_post(dostep); //podnosimy semafor
              	if (sig == SIGTSTP) {
                  	sigemptyset(&mask3);
                  	sigaddset(&mask3, SIGUSR1);
                  	sigprocmask(SIG_BLOCK, &mask3, NULL);
                  	sigwait(&mask3, &signallll);
                  	sem_wait(dostep);
                  	sig = *sh_mem_signal;
                  	sem_post(dostep);
                  	printf("P3 otrzymalem SIGUSR1\n");
                  	//kill(getpid(), sig);
                  	sigemptyset(&mask3);
                } else {
                  	sigaction(sig, &PactionD, NULL);
                   	kill(getpid(), sig); //P3 wykonuje odebrany sygnal
                    	sigaction(sig, &Paction3, NULL);
                }
            }
      } 
}

void pm_signal_capture(int sig, siginfo_t *info, void *ucontext) { 
	//printf("PM dostalem od p3 %d\n", sig);
  	sem_wait(dostep); //opuszczamy semafor
  	if (info->si_pid == *sh_mem_pid3) {
        	memcpy(sh_mem_signal, &sig, sizeof(int)); //wstawiamy signal do pamieci wspoldzielonej
      	}
  	kill(*sh_mem_pid1, SIGUSR1); //PM wysyla sygnal do P1 
  	sem_post(dostep);//podnosimy semafor
}

void p1_signal_capture(int sig, siginfo_t *info, void *ucontext) {
  	if (info->si_pid == getppid()) { //proces P1 upewnia sie czy dostal sygnal od PM
        	if (sig == SIGUSR1) { //proces P1 sprawdza czy jest to sygnal SIGUSR1
		      	sem_wait(dostep); //opuszczamy semafor
		      	sig = *sh_mem_signal; //P1 odbiera sygnal z pamieci wspoldzielonej
		      	kill(*sh_mem_pid2, SIGUSR1); //P1 wysyla sygnal do P2
		      	sem_post(dostep); //podnosimy semafor
		      	if (sig == SIGTSTP) {
		      		sigemptyset(&mask1);
                  		sigaddset(&mask1, SIGUSR1);
                  		sigprocmask(SIG_BLOCK, &mask1, NULL);
                  		sigwait(&mask1, &signallll);
                  		sem_wait(dostep);
                  		kill(*sh_mem_pid2, SIGUSR1);
                  		sig = *sh_mem_signal;
                  		sem_post(dostep);
                  		printf("P1 otrzymalem SIGUSR1\n");
                  		//kill(getpid(), sig);
                  		sigemptyset(&mask1);
		        } else {
		          	sigaction(sig, &PactionD, NULL);
		            	kill(getpid(), sig); //P1 wykonuje odebrany sygnal
		            	sigaction(sig, &Paction1, NULL);
		        }
            }
      }
}

void p2_signal_capture(int sig, siginfo_t *info, void *ucontext) {	
  	if (sig == SIGUSR1) { //proces P2 sprawdza czy jest to sygnal SIGUSR1
        	sem_wait(dostep); //opuszczamy semafor
        	if (info->si_pid == *sh_mem_pid1) { //proces P2 upewnia sie czy dostal sygnal od P1
		      	sig = *sh_mem_signal; //P2 odbiera sygnal z pamieci wspoldzielonej
		      	kill(*sh_mem_pid3, SIGUSR1); //P2 wysyla sygnal do P3
		      	sem_post(dostep); //podnosimy semafor
		      	if(sig == SIGTSTP){
		          	sigemptyset(&mask2);
		          	sigaddset(&mask2, SIGUSR1);
		          	sigprocmask(SIG_BLOCK, &mask1, NULL);
		          	sigwait(&mask2, &signallll);
		          	sem_wait(dostep);
		          	kill(*sh_mem_pid3, SIGUSR1);
		          	sig = *sh_mem_signal;
		          	sem_post(dostep);
		          	printf("P2 otrzymalem SIGUSR1\n");
		          	//kill(getpid(), sig);
		          	sigemptyset(&mask2);
		        } else {
		          	sigaction(sig, &PactionD, NULL);
		            	kill(getpid(), sig); //P3 wykonuje odebrany sygnal
		            	sigaction(sig, &Paction2, NULL);
		       }      		
            	}
        }
}


/*-------------------------Fulkcja p1 do trybu danych z pliku----------------*/
void proces1_case1(struct message_buffer m, int msg_id) 
{
	/* Uchwyt do odczytu pliku. */
	FILE * fPointer = fopen("data.txt", "r");
	
	/* Petla while, ktora czyta po jednej lini i wysyla ja do p2. */
        while(fgets(m.msg_text, sizeof(m.msg_text), fPointer) != NULL) {
		int len = strlen(m.msg_text);
		
		/* Przed wyslaniem wiadomosci jest z niej usuwany jeszcze
		   ostatni znak (jezeli jest znakiem konca lini).*/
		if (m.msg_text[len - 1] == '\n') {         	
			m.msg_text[len - 1] = 0;
		}
		printf("P1 Odczytalem\n");

		/* Wyslanie wiadomosci do kolejki. */
		if (msgsnd(msg_id, &m, sizeof(m.msg_text), 0) == -1)
		{
			//perror("msgsnd");
			exit(1);
        	}
        }
        /* Kiedy plik sie skonczy jest wysylana wiadomosc typu koniec
           do procesu 2, ktora oznacza koniec danych w pliku. */
        m.msg_type = KONIEC;
        m.msg_text[0] = 'a';
        
        /* Wyslanie wiadomosci do kolejki */
        if (msgsnd(msg_id, &m, sizeof(m.msg_text), 0) == -1)
        {
		//perror("msgsnd koniec");
		exit(1);
        }
        m.msg_type = DALEJ;
        fclose(fPointer);
        //printf("skonczylem p1\n");
}

/*-------------------------Funkcja p2 do trybu danych z stdin-----------------*/
void proces1_case2(struct message_buffer m, int msg_id, int kontynuacja) 
{
	/* Ponizsza petla jest po to zeby czytac dane w sposob ciagly. */
	while (kontynuacja == 1) {
	
		/* Program odczytuje dane z stdin jak z pliku i po kazdej
		   odczytanej lini wysyla dane dalej do p2. */
		while(fgets(m.msg_text, sizeof(m.msg_text), stdin) != NULL) {
			int len = strlen(m.msg_text);
			
			/* Przed wyslaniem usuwany jest znak konca lini. */
			if (m.msg_text[len - 1] == '\n') {         	
				m.msg_text[len - 1] = 0;
			}
			
			/* Typ wiadomosci jest ustawiony tak by proces 2
			   dalej nasluchiwal nowych danych. */
			m.msg_type = DALEJ;
			
			/* Jezeli uzytkownik wpisze . w nowej lini to
			   zostanie ustawiony typ na KONIEC i dzieki temu
			   proces 2 bedzie wiedzial by skonczyc prace. */
			if (m.msg_text[0] == '.') {
				m.msg_type = KONIEC;
				kontynuacja = 0;
				//printf("do p2 skoncz zabawe\n");
			}
			
			/* Wyslanie wiadomosci do kolejki. */
			if (msgsnd(msg_id, &m, sizeof(m.msg_text), 0) == -1)
			{
				//perror("msgsnd koniec");
				exit(1);
			}
			
			/* Proces 1 rowniez podejmuje dzialanie zatrzymania
			   kiedy ustawil typ na KONIEC. */
			if (m.msg_type == KONIEC) {
				break;
			}
		}
	}
	m.msg_type = DALEJ;
}

/*-------------------------Glowna funkcja procesu 1 (p1)----------------------*/
void proces1(int msg_id) {
	/* Odbieranie sygnalow */
	Paction1;
	Paction1.sa_flags = SA_RESTART|SA_SIGINFO;
	Paction1.sa_sigaction = &p1_signal_capture;
	sigaction(SIGTSTP, &Paction1, NULL);
	sigaction(SIGCONT, &Paction1, NULL);
	sigaction(SIGTERM, &Paction1, NULL);
	sigaction(SIGUSR1, &Paction1, NULL);
		
	/*signal(SIGTSTP, p1_signal_capture);
	
	signal(SIGCONT, p1_signal_capture);
	
	signal(SIGTERM, p1_signal_capture);*/
	 
	/* m jest tym co proces 1 bedzie wysylal dokolejki. */
	struct message_buffer m;
	m.msg_type = DALEJ;
	char in_line[100];
	char wybor;
	int kontynuacja = 1;
	int err;
	while (1) { 
		/* Uzytkownik podejmoje wybor. */
		wybor = 'a';
		
		/* Semafor zostanie podniesiony przez p3 kiedy
		   on skonczy wypisywac dzieki temu MENU bedzie 
		   zawsze na koniec a nie w trakcie wykonywania 
		   procesu 3. */
		sem_wait(wypisywanie);
		printf("MENU\n");
		printf("1) Plik\n2) stdin\n");
		/* Dziwna konstrukcja ponizej ale z jakiegos
		   powodu nie dzialalo pobieranie bez tego cuda. */
		err = scanf(" %c", &wybor);
		if (wybor == 'a') {
			err = scanf(" %c", &wybor);
		}
		if (err == EOF) {
			perror("scanf error");
		}
		kontynuacja = 1;
		      	
		switch(wybor) {
		case '1':
			proces1_case1(m, msg_id);           
			break;
		case '2':
			proces1_case2(m, msg_id, kontynuacja);
			break;
		default:
			break;     
		}
	}
}

/*-------------------------Glowna funkcja procesu 2 (p2)----------------------*/
void proces2(int msg_id, int msg2_id) {
	/* Odbieranie sygnalow */
	Paction2;
	Paction2.sa_flags = SA_RESTART|SA_SIGINFO;
	Paction2.sa_sigaction = &p2_signal_capture;
	sigaction(SIGTSTP, &Paction2, NULL);
	sigaction(SIGCONT, &Paction2, NULL);
	sigaction(SIGTERM, &Paction2, NULL);
	sigaction(SIGUSR1, &Paction2, NULL);

	/* end dopuszcza proces do dalszego dzialania. */
	int end = 1;
	while (1) {
		while (end == 1) {
			struct message_buffer m;
			int err;
			//m.msg_type = 1;
			
			/* Odebranie wiadomosci od p1. */				
			err = msgrcv(msg_id, &m, sizeof(m.msg_text), 0, 0);
					  
			if (err == -1)
			{
				//perror("msgrcv");
			}
			
			printf("P2 Odebralem\n");
			/* Rozpoczecie sekwencji do wyslania wiadomoosci do p3. */
			struct message2_buffer m2;
			
			/* Typ jest kopiowany dzieki czemu procesy 2 i 3 beda 
			   wiedziec czy maja czekac na dalsze dane. */
			m2.msg_type = m.msg_type;
			m2.msg_text = strlen(m.msg_text);
			
			/* Wyslanie wiadomosci z wykozystanie innej struktury. */
			if (msgsnd(msg2_id, &m2, sizeof(m2.msg_text), 0) == -1)
			{
				perror("msgsnd p2");
			}
			
			/* Jezeli to co zostalo otrzymane od p1 ma typ KONIEC
			   to proces 2 nie czeka na dalesze dane. */
			if (m.msg_type == KONIEC) {
				//printf("Koncze\n");
				break; 
			}
		}
	}
}

/*-------------------------Glowna funkcja procesu 3 (p3)----------------------*/
void proces3(int msg2_id) {
	/* Odbieranie sygnalow */
	Paction3;
	Paction3.sa_flags = SA_RESTART|SA_SIGINFO;
	Paction3.sa_sigaction = &p3_signal_capture;
	sigaction(SIGTSTP, &Paction3, NULL);
	sigaction(SIGCONT, &Paction3, NULL);
	sigaction(SIGTERM, &Paction3, NULL);
	sigaction(SIGUSR1, &Paction3, NULL);
	int end = 1;
	while (1) {
		while (end == 1) {
			struct message2_buffer m2;
			int err;
			
			/* Odebranie wiadomosci od procesu 2. */
			err = msgrcv(msg2_id, &m2, sizeof(m2.msg_text), 0, 0);
			
			/* Jezeli ta wiadomosc jest typu KONIEC to p3 konczy
			   dzialanie petli i umozliwia p1 zapisanie nowego MENU
			   za pomoca podniesienia semafora. */
			if (m2.msg_type == KONIEC) {
				//printf("p3 placze z radosci pod scena\n");
				//printf("podnozse semafor\n");
				sem_post(wypisywanie);
				break; 
			} else {
				printf("[%d]->[%d] %d\n", getppid(), getpid(), m2.msg_text);
			} 
			if (err == -1)
			{
				perror("msgrcv p3");
			}
		}
	}
}

/*-------------------------Main czyli proces maciezysty PM--------------------*/
int main() {
	/* Odbieranie sygnalow */
	PactionD;
	PactionD.sa_handler = SIG_DFL;
	
	PactionM;
	PactionM.sa_flags = SA_RESTART|SA_SIGINFO;
	PactionM.sa_sigaction = &pm_signal_capture;
	sigaction(SIGTSTP, &PactionM, NULL);
	sigaction(SIGCONT, &PactionM, NULL);
	sigaction(SIGTERM, &PactionM, NULL);
	sigaction(SIGUSR1, &PactionM, NULL);
	
	//struct process_states *shmem;
	
	/* Tworzenie pamieci wspoldzielonej */
	sh_mem_pid1 = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	sh_mem_pid2 = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	sh_mem_pid3 = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	sh_mem_signal = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		
	/* Tworzenie semaforow, p1 dla wypisywania p1 i dla pamieci wspodzielonej. */
	wypisywanie = sem_open("wypisywanie", O_CREAT, 0666, 1);
	dostep = sem_open("dostep", O_CREAT, 0666, 1);
	
	/*sem_close(wypisywanie);
	sem_unlink("wypisywanie");
	sem_close(dostep);
	sem_unlink("dostep");*/
	
	/* Deskryptory procesow potomnych */
	pid_t pid1;
	pid_t pid2;
	pid_t pid3;
	
	/* Tworzenie kolejek komunikatow msg_id-dla komunikacji
	   p1 -> p2, a msg2_id dla komunikacji p2 -> p3.*/
	int msg_id;
	int msg2_id;
	msg_id = msgget(IPC_PRIVATE, 0600 | IPC_CREAT | IPC_EXCL);
	msg2_id = msgget(IPC_PRIVATE, 0600 | IPC_CREAT | IPC_EXCL);
	if (msg_id == -1  || msg2_id == -1)
	{
		perror("msgget");
	}
	
	/* Tworzenie drzewa procesow 
	         |PM|
	       /  |   \
	    |P1| |P2| |P3|
	    za pomoca fork. */
	pid1 = fork();
	switch(pid1)
	{
		case -1:
			perror("fork");
			break;
		case 0:
			proces1(msg_id);
			break;
		default:
			pid2 = fork();
			switch ( pid2 )
			{
				case 0:
					proces2(msg_id, msg2_id);
					break;
				case -1:
					perror("fork");
					break;
				default:
					pid3 = fork();
					switch( pid3 )
					{
						case 0:
							proces3(msg2_id);
							break;
						case -1:
							perror("fork");
							break;
						default:
							/* Zapis podstawowych danych do shmem. */
							sem_wait(dostep);
							memcpy(sh_mem_pid1, &pid1, sizeof(int));
							memcpy(sh_mem_pid2, &pid2, sizeof(int)); 
							memcpy(sh_mem_pid3, &pid3, sizeof(int));
							sem_post(dostep);
							/* Zapis do pamieci wspoldzielonej pidow */
					}
			}
			break;
	}
	
	/*-------------------------Czyszczenie po sobie-------------------------*/
	
	/* Oczekiwanie na zakonczenie sie procesu 1*/
	wait(pid1);
	
	/* Oczekiwanie na zakonczenie sie wszystkich procesow potomncy */
	while (wait(NULL) > 0) {}
	
	/* Usuwanie urzytych wczesniej semaforow */
	sem_close(wypisywanie);
	sem_unlink("wypisywanie"); 
  	sem_close(dostep); 
  	sem_unlink("dostep");
	
	/* Usuwanie urzytych wczesniej kolejek */
	//msgctl(msg_id, IPC_RMID/*IPC_NOWAIT*/, NULL);
	//msgctl(msg2_id, IPC_RMID/*IPC_NOWAIT*/, NULL);
	return 0;
}
