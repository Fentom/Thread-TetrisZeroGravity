#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include "GrilleSDL.h"
#include "Ressources.h"
#include "ClientTetris.h"

// Bonus au score
#define COLONNE_COMPLETE 5
#define LIGNE_COMPLETE 5

// Vitesse de défilement du message (en nanosecondes)
#define TEXT_SPEED 400000000

// Dimensions de la grille de jeu
#define NB_LIGNES   14
#define NB_COLONNES 20

// Nombre de cases maximum par piece
#define NB_CASES    4

// Macros utlisees dans le tableau tab
#define VIDE        0

int tab[NB_LIGNES][NB_COLONNES]
={ 
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};

typedef struct
{
	int ligne;
	int colonne;
} CASE;

typedef struct
{
	CASE cases[NB_CASES];
	int  nbCases;
	int  professeur;
} PIECE;

PIECE pieces[7] = 
{ 0,0,0,1,1,0,1,1,4,WAGNER,       // carre
0,0,1,0,2,0,2,1,4,MERCENIER,    // L
0,1,1,1,2,0,2,1,4,VILVENS,      // J
0,0,0,1,1,1,1,2,4,DEFOOZ,       // Z
0,1,0,2,1,0,1,1,4,GERARD,       // S
0,0,0,1,0,2,1,1,4,CHARLET,      // T
0,0,0,1,0,2,0,3,4,MADANI };     // I

int cleServer = 0; // Contiendra la clé du serveur en cas de jeu online
void terminerProgramme();

//////////////////////////////// Thread Defilement de message /////////////////////////////////////

char *message;        // pointeur vers le message à defiler
int tailleMessage;    // longueur du message
int indiceCourant;    // indice du premier caractère à afficher dans la zone graphique
pthread_mutex_t mutexMessage;

void * threadDefileMessage(void *);
void setMessage(const char *texte);
void free_message(void* p);

///////////////////////////////////// Thread piece ////////////////////////////////////////////////

PIECE pieceEnCours;

void * threadPiece(void *);
void RotationPiece(PIECE *pPiece);
void TranslationCases(CASE *vect, int nbCases);
int compare_case(const void* a, const void* b);

///////////////////////////////////// Thread Event ////////////////////////////////////////////////

int nbCasesInserees = 0; // nombre de cases actuellement insérées par le joueur.
CASE casesInserees[NB_CASES]; // cases insérées par le joueur
pthread_cond_t condCasesInserees;
pthread_mutex_t mutexCasesInserees;

void * threadEvent(void *);

///////////////////////////////////// Thread Score ////////////////////////////////////////////////

int score = 0;
int MAJScore = 0;
pthread_cond_t condScore;
pthread_mutex_t mutexScore; 

void * threadScore(void *);

///////////////////////////////////// Thread Case ////////////////////////////////////////////////

int lignesCompletes[4];
int nbLignesCompletes = 0;
int colonnesCompletes[4];
int nbColonnesCompletes = 0;
int nbAnalyses = 0;
pthread_key_t cle;
pthread_mutex_t mutexAnalyse;

void * threadCase(void *);
void sigUsr1Handler(int sig);
int analyseLigne(int ligne);
int analyseColonne(int col);
int recherche_int(int valeur, int* tab, int nbElem);
void free_case(void*p);

///////////////////////////////////// Thread Gravite //////////////////////////////////////////////

pthread_cond_t condAnalyse;

void * threadGravite(void *);
int compare_ints(const void* a, const void* b);

///////////////////////////////////// Thread FinPartie ////////////////////////////////////////////

void * threadFinPartie(void *);
void sigUsr2Handler(int sig);

///////////////////////////////////// Thread Joueurs Connectés ////////////////////////////////////

void * threadJoueursConnectes(void *);
void sigHupHandler(int sig);

///////////////////////////////////// Thread Top Score ////////////////////////////////////////////

void * threadTopScore(void *);
void sigQuitHandler(int sig);
void finTopScore(void* p);

///////////////////////////////////// Synchronisation générale ////////////////////////////////////

bool traitementEnCours = 0;
pthread_mutex_t mutexTraitement;
pthread_mutex_t mutexPieceEnCours;

///////////////////////////////////////////////////////////////////////////////////////////////////

pthread_t threadHandleCase[NB_LIGNES][10];
pthread_t threadHandleMessage, threadHandlePiece, threadHandleEvent, threadHandleScore;
pthread_t threadHandleGravite, threadHandleFinPartie, threadHandleJoueurs, threadHandleTopScore;

int main(int argc,char* argv[])
{
	int i, j;
	CASE *pcase;
	char buffer[80];
	srand((unsigned)time(NULL)); // Pour permettre d'avoir des v. aléatoires.
	pieceEnCours.nbCases = 4;

	printf("(THREAD MAIN) Masquage de tous les signaux\n");
	sigset_t mask;
	sigfillset(&mask);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	//Initialisation des mutex, variables de conditions et clés d'accès aux variables spécifiques
	pthread_mutex_init(&mutexMessage, NULL);
	pthread_mutex_init(&mutexCasesInserees, NULL);
	pthread_mutex_init(&mutexScore, NULL);
	pthread_mutex_init(&mutexAnalyse, NULL);
	pthread_mutex_init(&mutexTraitement, NULL);
	pthread_cond_init(&condCasesInserees, NULL);
	pthread_cond_init(&condScore, NULL);
	pthread_cond_init(&condAnalyse, NULL);
	pthread_key_create(&cle, free_case);

	// Ouverture de la grille de jeu (SDL)
	printf("(THREAD MAIN) Ouverture de la grille de jeu\n");
	fflush(stdout);
	sprintf(buffer,"!!! TETRIS ZERO GRAVITY !!!");

	if (OuvrirGrilleSDL(NB_LIGNES,NB_COLONNES,40,buffer) < 0)
	{
		printf("Erreur de OuvrirGrilleSDL\n");
		fflush(stdout);
		exit(1);
	}

// Chargement des sprites et de l'image de fond
	ChargementImages();
	DessineSprite(12,11,VOYANT_VERT);

	printf("Nombre d'arguments: %d\n", argc);
	if (argc > 1)
	{
		if ( argc != 3)
		{
			printf("Il faut executer le programme de la façon suivante : ./Tetris cleServer pseudo\n");
			exit(1);
		}

///////////////////////////////////////// Mode online /////////////////////////////////////////////
		cleServer = atoi(argv[1]);
		printf("Connexion au serveur...\n");
		if (ConnectionServeur( (key_t)cleServer, argv[2]) == 0)
		{
			printf("Connexion réussie.\n");
//Creation du thread joueurs connectés ////////////////////////////////////////////////////////////
			if (errno = pthread_create(&threadHandleJoueurs, NULL, threadJoueursConnectes, NULL) !=0 )
				perror("Erreur de lancement threadJoueursConnectes");

//Creation du thread de TopScore //////////////////////////////////////////////////////////////////
			if (errno = pthread_create(&threadHandleTopScore, NULL, threadTopScore, NULL) !=0 )
				perror("Erreur de lancement threadTopScore");
		}
		else
		{
			printf("\n---------------------------\nImpossible d'établir la connexion au serveur\n");
		}
	}

//Creation du thread de defilement du message /////////////////////////////////////////////////////

	setMessage("Bienvenue dans Tetris Zero Gravity");

	if (errno = pthread_create(&threadHandleMessage, NULL, threadDefileMessage, NULL) !=0 )
		perror("Erreur de lancement threadDefileMessage");

//Creation du thread de defilement du message /////////////////////////////////////////////////////

	if (errno = pthread_create(&threadHandlePiece, NULL, threadPiece, NULL) !=0 )
		perror("Erreur de lancement threadPiece");

//Creation du thread Event ////////////////////////////////////////////////////////////////////////

	if (errno = pthread_create(&threadHandleEvent, NULL, threadEvent, NULL) !=0 )
		perror("Erreur de lancement threadEvent");

//Creation du thread Score ////////////////////////////////////////////////////////////////////////

	if (errno = pthread_create(&threadHandleScore, NULL, threadScore, NULL) !=0 )
		perror("Erreur de lancement threadScore");

	/**/printf("(Main) Thread Score cree. errno = %d\n", (int)errno);

//Creation des threads Case ///////////////////////////////////////////////////////////////////////

	for (i = 0 ; i < NB_LIGNES ; ++i)
	{
		for (j = 0 ; j < 10 ; ++j)
		{
			pcase = (CASE*)malloc(sizeof(CASE));
			pcase->ligne = i;
			pcase->colonne = j;
			if (errno = pthread_create(&threadHandleCase[i][j], NULL, threadCase, (void*)pcase)!=0)
				perror("Erreur de lancement threadCase");
		}
	}

//Creation du thread Gravité //////////////////////////////////////////////////////////////////////

	if (errno = pthread_create(&threadHandleGravite, NULL, threadGravite, NULL) !=0 )
		perror("Erreur de lancement threadGravite");

//Creation du thread FinPartie ////////////////////////////////////////////////////////////////////

	if (errno = pthread_create(&threadHandleFinPartie, NULL, threadFinPartie, NULL) !=0 )
		perror("Erreur de lancement threadFinPartie");


///////////////////////////////////////////////////////////////////////////////////////////////////

	printf("(Main) Pret a jouer.\n");

	// Attente de la fin de la partie
	pthread_join(threadHandleFinPartie, NULL);
	terminerProgramme();
}



void setMessage(const char *texte)
{
	pthread_mutex_lock(&mutexMessage);

	if(message != NULL)
		free(message);

	tailleMessage = strlen(texte)+8; // Pour avoir du blanc au début
	indiceCourant = 0;
	message = (char *) malloc(tailleMessage+1+8); // 1 pour \0 et 8 pour les 8 blancs de la fin
	strcpy(message, "        ");
	strcpy(message+8, texte);
	strcpy(message+8+strlen(texte), "        ");

	printf("Taille du message: %d\n Message: ", (int)strlen(texte));
	printf("%s\n", texte);
	printf("--------------------------------------------\n");

	pthread_mutex_unlock(&mutexMessage);
}

void * threadDefileMessage(void *p)
{
	int i;
	struct timespec temps;
	temps.tv_sec = 0;
	temps.tv_nsec = TEXT_SPEED;

	pthread_cleanup_push(free_message, NULL);

	while (1)
	{
		pthread_mutex_lock(&mutexMessage);

		if (indiceCourant > tailleMessage)
			indiceCourant = 0;

		for (i = 0 ; i < 8 ; ++i)
		{
			DessineLettre(10, 11+i, message[indiceCourant+i]);
		}
		++indiceCourant;
		pthread_mutex_unlock(&mutexMessage);

		nanosleep(&temps, NULL);
	}

	pthread_cleanup_pop(1);
}

void free_message(void* p)
{
	free(message);
	// printf("Libération de l'espace mémoire pour le message\n");
}


void * threadPiece(void *p)
{
	int rot, i, j, col_min, ligne_min, pieceValidee;
	CASE casesVerif[NB_CASES];

	pthread_mutex_lock(&mutexPieceEnCours); // petit lock pour pouvoir unlock juste en dessous :D

	while (1)
	{
		pieceEnCours = pieces[rand() % 7];
		// pieceEnCours = pieces[0];
		rot = rand() % 4;
		
		for (i = 0 ; i < rot ; ++i)
			RotationPiece(&pieceEnCours);

		TranslationCases(pieceEnCours.cases, pieceEnCours.nbCases);

		// Tri sur ligne/colonne
		qsort(pieceEnCours.cases, pieceEnCours.nbCases , sizeof(CASE), compare_case);

		// On autorise le thread fin à vérifier si on peut insérer la nouvelle pièce
		pthread_mutex_unlock(&mutexPieceEnCours);

		// Effacement de la zone "pièce en cours"
		for (i = 0 ; i < 4 ; ++i)
		{
			for (j = 0 ; j < 4 ; ++j)
			{
				EffaceCarre(3 + i , 15 + j );
			}
		}

		// Affichage de la pièce en cours:
		for (i = 0 ; i < pieceEnCours.nbCases ; ++i)
		{
			DessineSprite(3 + pieceEnCours.cases[i].ligne, 15 + pieceEnCours.cases[i].colonne, pieceEnCours.professeur);
		}

		pieceValidee = 0;
		while (!pieceValidee)
		{
			// On attend que toutes les pièces soient insérées par le joueur
			pthread_mutex_lock(&mutexCasesInserees);
			while (nbCasesInserees < pieceEnCours.nbCases)
			{
				pthread_cond_wait(&condCasesInserees, &mutexCasesInserees);
			}
			pthread_mutex_unlock(&mutexCasesInserees);

			// Tri des cases insérées
			qsort(casesInserees, nbCasesInserees, sizeof(CASE), compare_case);

			// Vérification de la position des cases insérées par rapport à la pièce en cours
			memcpy(casesVerif, casesInserees, sizeof(CASE) * NB_CASES);
			TranslationCases(casesVerif, nbCasesInserees);

			pieceValidee = 1;
			for (i = 0 ; i < nbCasesInserees && pieceValidee == 1 ; ++i)
			{
				if (compare_case(&(pieceEnCours.cases[i]), &(casesVerif[i])) != 0)
					pieceValidee = 0;
			}

			if (pieceValidee)
			{
				for (i = 0 ; i < nbCasesInserees ; ++i)
				{
					tab[casesInserees[i].ligne][casesInserees[i].colonne] = BRIQUE;
					DessineSprite(casesInserees[i].ligne, casesInserees[i].colonne, BRIQUE);
				}

				// On réveille le threadScore et on incrémente le score
				pthread_mutex_lock(&mutexScore);
				++score;
				MAJScore = 1;
				pthread_mutex_unlock(&mutexScore);
				pthread_cond_signal(&condScore);

				// On empêche le thread Fin de lire la pièce avant que la nouvelle ne soit générée
				pthread_mutex_lock(&mutexPieceEnCours);

				// On réveille les 4 threads case
				for (i = 0 ; i < nbCasesInserees ; ++i)
				{
					pthread_kill(threadHandleCase[casesInserees[i].ligne][casesInserees[i].colonne], SIGUSR1);
				}
			}
			else
			{
				for (i = 0 ; i < nbCasesInserees ; ++i)
				{
					EffaceCarre(casesInserees[i].ligne, casesInserees[i].colonne);
					tab[casesInserees[i].ligne][casesInserees[i].colonne] = VIDE;
				}

				pthread_mutex_lock(&mutexTraitement);
				traitementEnCours = 0;
				pthread_mutex_unlock(&mutexTraitement);
				DessineSprite(12, 11, VOYANT_VERT);
			}
			pthread_mutex_lock(&mutexCasesInserees);
			nbCasesInserees = 0;
			pthread_mutex_unlock(&mutexCasesInserees);
		}
	}
}

void RotationPiece(PIECE *pPiece)
{
	PIECE ancienne = *pPiece;
	int i;

	//Rotation
	for (i = 0 ; i < pPiece->nbCases ; ++i)
	{
		pPiece->cases[i].ligne = -1 * ancienne.cases[i].colonne;
		pPiece->cases[i].colonne = ancienne.cases[i].ligne;
	}
}

void TranslationCases(CASE *vect, int nbCases)
{
	int i;
	int ligne_min = vect[0].ligne;
	int col_min = vect[0].colonne;
	
	// Recherche des valeurs minimales
	for (i = 0 ; i < nbCases ; ++i)
	{
		if (vect[i].colonne < col_min)
			col_min = vect[i].colonne;
		if (vect[i].ligne < ligne_min)
			ligne_min = vect[i].ligne;
	}

	// Translation afin de rendre la pièce positive sur (0,0)
	if (ligne_min || col_min)
	{
		for (i = 0 ; i < nbCases ; ++i)
		{
			vect[i].ligne = vect[i].ligne - ligne_min; 
			vect[i].colonne = vect[i].colonne - col_min;
		}
	}
}

int compare_case(const void* a, const void* b)
{
	const CASE *ca = (const CASE*)a;
	const CASE *cb = (const CASE*)b;

	if (ca->ligne < cb->ligne)
		return -1;
	if (ca->ligne > cb->ligne)
		return 1;

	if (ca->colonne < cb->colonne)
		return -1;
	if (ca->colonne > cb->colonne)
		return 1;

	return 0;
}

void * threadEvent(void *p)
{
	int i;
	char ok = 0;
	EVENT_GRILLE_SDL event;
	struct timespec bad_click_time;
	bad_click_time.tv_sec = 0;
	bad_click_time.tv_nsec = 250000000;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while(!ok)
	{
		event = ReadEvent();
		switch(event.type)
		{
			case CROIX:
				ok = 1;
				break;

			case CLIC_GAUCHE:
				if (!traitementEnCours)
				{
					if (event.colonne < 10 && nbCasesInserees < 4)
					{
						if (tab[event.ligne][event.colonne] == VIDE)
						{
							DessineSprite(event.ligne, event.colonne, pieceEnCours.professeur);

							pthread_mutex_lock(&mutexCasesInserees);
							casesInserees[nbCasesInserees].ligne = event.ligne;
							casesInserees[nbCasesInserees].colonne = event.colonne;
							++nbCasesInserees;
							pthread_mutex_unlock(&mutexCasesInserees);
							pthread_cond_signal(&condCasesInserees);

							tab[event.ligne][event.colonne] = pieceEnCours.professeur;
							// printf("\ntab[%d][%d] = %d\n", event.ligne, event.colonne, tab[event.ligne][event.colonne]);

							if (nbCasesInserees == 4)
							{
								pthread_mutex_lock(&mutexTraitement);
								traitementEnCours = 1;
								pthread_mutex_unlock(&mutexTraitement);
								DessineSprite(12, 11, VOYANT_BLEU);
							}
						}
					}
					else
					{
						// Click hors de la zone de jeu (traitement = 0)
						DessineSprite(12, 11, VOYANT_ROUGE);
						nanosleep(&bad_click_time, NULL);
						DessineSprite(12, 11, VOYANT_VERT);
					}
				}
				else
				{
					// Si on clique pendant un traitement
					DessineSprite(12, 11, VOYANT_ROUGE);
					nanosleep(&bad_click_time, NULL);
					DessineSprite(12, 11, VOYANT_BLEU);
				}
				break;

			case CLIC_DROIT:
				printf("Clic droit recu: nous allons effacer %d cases.\n", nbCasesInserees);

				for (i = 0 ; i < nbCasesInserees ; ++i)
				{
					EffaceCarre(casesInserees[i].ligne, casesInserees[i].colonne);
					tab[casesInserees[i].ligne][casesInserees[i].colonne] = VIDE;
				}

				pthread_mutex_lock(&mutexCasesInserees);
				nbCasesInserees = 0;
				pthread_mutex_unlock(&mutexCasesInserees);

			case CLAVIER:
				break;
		}
	}
	// Quitter le jeu (réveil du Main)
	pthread_cancel(threadHandleFinPartie);
}

void * threadScore(void *p)
{
	int i;

	pthread_cleanup_push(finTopScore, NULL);

	for (i = 0 ; i < 4 ; ++i)
	{
		DessineChiffre(1, 18-i, (int) (0/pow(10, i)) % 10); //Cette monstruosité permet d'obtenir le chiffre à l'indice i du score
	}

	while (1)
	{
		pthread_mutex_lock(&mutexScore);
		while (!MAJScore)
		{
			pthread_cond_wait(&condScore, &mutexScore);
		}
		pthread_mutex_unlock(&mutexScore);

		for (i = 0 ; i < 4 ; ++i)
		{
			DessineChiffre(1, 18-i, (int) (score/pow(10, i)) % 10); //Cette monstruosité permet d'obtenir le chiffre à l'indice i du score
		}

		pthread_mutex_lock(&mutexScore);
		MAJScore = 0;
		pthread_mutex_unlock(&mutexScore);
	}
	pthread_cleanup_pop(1);
}

void finTopScore(void* p)
{
	if (cleServer)
	{
		printf("Envoi du Score au serveur...\n");
		// Si l'on a battu le meilleur score
		switch(EnvoiScore(cleServer, score))
		{
			case 0:
				setMessage("Game Over");
				printf("Pas de nouveau record\n");
				break;

			case 1:
				setMessage("Nouveau Record");
				printf("Nouveau record!\n");
				break;

			case -1:
				setMessage("Connexion serveur perdue");
				printf("(threadScore) Envoi du score impossible: connexion au serveur perdue\n");
				break;
		}

		int i;
		TOPSCORE TopScore;

		if(GetTopScore((key_t)cleServer, &TopScore) == -1)
			setMessage("Connexion au serveur perdue");
		else
		{
			for (i = 0 ; i < 4 ; ++i)
			{
				DessineChiffre(8, 18-i, (int) (TopScore.score/pow(10, i)) % 10); //Cette monstruosité permet d'obtenir le chiffre à l'indice i
			}
		}
	}
}

void * threadCase(void *p)
{
	CASE *pcase = (CASE*)p;
	struct sigaction sigact;

	// Armement de SIGUSR1
	sigact.sa_handler = sigUsr1Handler;
	sigaction(SIGUSR1, &sigact, NULL);

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaddset(&sigact.sa_mask, SIGUSR1);
	pthread_sigmask(SIG_UNBLOCK, &sigact.sa_mask, NULL);

	pthread_setspecific(cle, (void*)pcase);

	while (1)
	{
		pause(); // Attente de SIGUSR1
	}
}

void free_case(void *p)
{
	CASE* pcase;

	pcase = (CASE*)pthread_getspecific(cle);
	// printf("suppression de la case du thread osef");

	free(pcase);
}

void sigUsr1Handler(int sig)
{
	int i;
	CASE *pcase;
	pcase = (CASE*)pthread_getspecific(cle);

	if (analyseLigne(pcase->ligne))
	{
		for (i = 0 ; i < 10 ; ++i)
		{
			DessineSprite(pcase->ligne, i, FUSION);
			tab[pcase->ligne][i] = FUSION;
		}

		pthread_mutex_lock(&mutexAnalyse);
		if (!recherche_int(pcase->ligne, lignesCompletes, nbLignesCompletes))
		{
			lignesCompletes[nbLignesCompletes] = pcase->ligne;
			++nbLignesCompletes;
		}
		pthread_mutex_unlock(&mutexAnalyse);
	}
	if (analyseColonne(pcase->colonne))
	{
		for (i = 0 ; i < NB_LIGNES ; ++i)
		{
			DessineSprite(i, pcase->colonne, FUSION);
			tab[i][pcase->colonne] = FUSION;
		}

		pthread_mutex_lock(&mutexAnalyse);
		if (!recherche_int(pcase->colonne, colonnesCompletes, nbColonnesCompletes))
		{
			colonnesCompletes[nbColonnesCompletes] = pcase->colonne;
			++nbColonnesCompletes;
		}
		pthread_mutex_unlock(&mutexAnalyse);
	}

	pthread_mutex_lock(&mutexAnalyse);
	++nbAnalyses;
	pthread_mutex_unlock(&mutexAnalyse);
	pthread_cond_signal(&condAnalyse);
}

int analyseLigne(int ligne)
{
	int complete = 1, i;

	for (i = 0; i < 10 && complete; ++i)
	{
		if (tab[ligne][i] != BRIQUE && tab[ligne][i] != FUSION)
			complete = 0;
	}
	return complete;
}

int analyseColonne(int col)
{
	int complete = 1, i;

	for (i = 0; i < NB_LIGNES && complete; ++i)
	{
		if (tab[i][col] != BRIQUE && tab[i][col] != FUSION)
			complete = 0;
	}
	return complete;
}

int recherche_int(int valeur, int* tab, int nbElem)
{
	int i;

	for (i = 0 ; i < nbElem ; ++i)
	{
		if (tab[i] == valeur)
			return 1;
	}
	return 0;
}

void * threadGravite(void *)
{
	int i, j, x, y;
	struct timespec observation, effet_graphique;
	observation.tv_sec = 2;
	observation.tv_nsec = 0;
	effet_graphique.tv_sec = 0;
	effet_graphique.tv_nsec = 500000000;

	while (1)
	{
		// On attend que toutes les analyses soient effectuées
		pthread_mutex_lock(&mutexAnalyse);
		while (nbAnalyses < pieceEnCours.nbCases)
		{
			pthread_cond_wait(&condAnalyse, &mutexAnalyse);
		}
		pthread_mutex_unlock(&mutexAnalyse);

		if (nbLignesCompletes == 0 && nbColonnesCompletes == 0)
		{
			printf("\n(GRAVITE) Rien d'intéressant dans ce move\n");
			pthread_mutex_lock(&mutexAnalyse);
			nbAnalyses = 0;
			pthread_mutex_unlock(&mutexAnalyse);
		}
		else // Il fat supprimer des lignes/colonnes
		{
			printf("(GRAVITE) Gravité activée!\n");
			printf("(GRAVITE) Suppression de %d lignes et %d colonnes:\n", nbLignesCompletes, nbColonnesCompletes);
			nanosleep(&observation, NULL);

			if (nbColonnesCompletes != 0)
			{
				// Tri des colonnes complètes afin de supprimer d'abbord les colonnes de gauche
				pthread_mutex_lock(&mutexAnalyse);
				qsort(colonnesCompletes, nbColonnesCompletes, sizeof(int), compare_ints);
				pthread_mutex_unlock(&mutexAnalyse);

				for (x = 0, y = 0 ; x < nbColonnesCompletes ; ++x) // Traitement pour chaque colonne complète
				{
					// Colonnes de gauche
					if (colonnesCompletes[x] < 5)
					{
						// Décalage des éléments vers la droite
						for (i = 0 ; i < NB_LIGNES ; ++i)
						{
							for (j = colonnesCompletes[x] ; j > 0 ; --j)
							{
								tab[i][j] = tab[i][j-1];
								if (tab[i][j] == VIDE)
									EffaceCarre(i, j);
								else
									DessineSprite(i, j, tab[i][j]);
							}
							tab[i][0] = VIDE;
							EffaceCarre(i, 0);
						}
						++y; // Y comptera le nombre de fois que nous avons traité des colonnes de gauche
						printf("Colonne %d supprimée.\n", colonnesCompletes[x]);
					}
					else // Colonnes de droite
					{
						// Décalage des éléments vers la gauche
						for (i = 0 ; i < NB_LIGNES ; ++i)
						{
							for (j = colonnesCompletes[nbColonnesCompletes-1-x + y] ; j < 9 ; ++j)
							{
								tab[i][j] = tab[i][j+1];
								if (tab[i][j] == VIDE)
									EffaceCarre(i, j);
								else
									DessineSprite(i, j, tab[i][j]);
							}
							tab[i][9] = VIDE;
							EffaceCarre(i, 9);
						}
						printf("Colonne %d supprimée.\n", colonnesCompletes[nbColonnesCompletes-1-x + y]);
					}

					nanosleep(&effet_graphique, NULL);
				}
			}

			if (nbLignesCompletes != 0)
			{
				// Tri des lignes complètes afin de supprimer d'abbord les lignes du haut
				pthread_mutex_lock(&mutexAnalyse);
				qsort(lignesCompletes, nbLignesCompletes, sizeof(int), compare_ints);
				pthread_mutex_unlock(&mutexAnalyse);

				for (x = 0, y = 0 ; x < nbLignesCompletes ; ++x) // Traitement pour chaque ligne complète
				{
					// Lignes du haut
					if (lignesCompletes[x] < 7)
					{
						// Décalage des éléments vers le bas
						for (j = 0 ; j < 10 ; ++j)
						{
							for (i = lignesCompletes[x] ; i > 0 ; --i)
							{
								tab[i][j] = tab[i-1][j];
								if (tab[i][j] == VIDE)
									EffaceCarre(i, j);
								else
									DessineSprite(i, j, tab[i][j]);
							}
							tab[0][j] = VIDE;
							EffaceCarre(0, j);
						}
						++y; // Y Comptera le nombre de fois que l'on a traité des lignes en haut
						printf("Ligne %d supprimée.\n", lignesCompletes[x]);
					}
					else // Lignes du bas
					{
						// Décalage des éléments vers le haut
						for (j = 0 ; j < 10 ; ++j)
						{
							for (i = lignesCompletes[nbLignesCompletes-1-x+y] ; i < NB_LIGNES-1 ; ++i)
							{
								tab[i][j] = tab[i+1][j];
								if (tab[i][j] == VIDE)
									EffaceCarre(i, j);
								else
									DessineSprite(i, j, tab[i][j]);
							}
							tab[NB_LIGNES-1][j] = VIDE;
							EffaceCarre(NB_LIGNES-1, j);
						}
						printf("Ligne %d supprimée.\n", lignesCompletes[nbLignesCompletes-1-x+y]);
					}
					
					nanosleep(&effet_graphique, NULL);
				}
			}

			// Ajout des points au score
			pthread_mutex_lock(&mutexScore);
			score += nbColonnesCompletes * COLONNE_COMPLETE;
			score += nbLignesCompletes * LIGNE_COMPLETE;
			MAJScore = 1;
			pthread_mutex_unlock(&mutexScore);
			pthread_cond_signal(&condScore);

			pthread_mutex_lock(&mutexAnalyse);
			nbColonnesCompletes = 0;
			nbLignesCompletes = 0;
			nbAnalyses = 0;
			pthread_mutex_unlock(&mutexAnalyse);
		}
		// Dans tous les cas:
		pthread_kill(threadHandleFinPartie, SIGUSR2);
	}
}

int compare_ints(const void* a, const void* b)
{
    const int *A = (const int*)a;
    const int *B = (const int*)b;
 
    return *A - *B;
}

void * threadFinPartie(void *p)
{
	int j, i, k, emplacement_possible;
	struct sigaction sigact;

	// Armement de SIGUSR2
	sigact.sa_handler = sigUsr2Handler;
	sigaction(SIGUSR2, &sigact, NULL);

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaddset(&sigact.sa_mask, SIGUSR2);
	pthread_sigmask(SIG_UNBLOCK, &sigact.sa_mask, NULL);

	while (1)
	{
		pause(); // Attente de SIGUSR2
		// Réception de SIGUSR2
		printf("(FIN_PARTIE) J'ai recu SIGUSR2 et je me réveillle\n");
		pthread_mutex_lock(&mutexPieceEnCours); // Afin d'attendre la si nouvelle pièce n'a pas encore été générée
		for (i = 0, emplacement_possible = 0 ; i < NB_LIGNES && !emplacement_possible ; ++i)
		{
			for (j = 0 ; j < 10 && !emplacement_possible ; ++j)
			{
				for (k = 0, emplacement_possible = 1 ; k < pieceEnCours.nbCases && emplacement_possible ; k++)
				{
					if (i+pieceEnCours.cases[k].ligne > NB_LIGNES-1 || j+pieceEnCours.cases[k].colonne > 9)
						emplacement_possible = 0;
					else if (tab[i+pieceEnCours.cases[k].ligne][j+pieceEnCours.cases[k].colonne] != VIDE)
						emplacement_possible = 0;
				}

				printf("(FIN_PARTIE) Emplacement possible = %d en (%d, %d)\n", emplacement_possible, i, j);
			}
		}
		pthread_mutex_unlock(&mutexPieceEnCours);

		if (!emplacement_possible)
		{
			printf("\n-------------------------------------------\n");
			printf("Plus de place dans la grille...\nFin de la partie.\n");
			printf("-------------------------------------------\n");
			pthread_exit(NULL);
		}
		else
		{
			pthread_mutex_lock(&mutexTraitement);
			traitementEnCours = 0;
			pthread_mutex_unlock(&mutexTraitement);
			DessineSprite(12, 11, VOYANT_VERT);
		}
	}
}

void sigUsr2Handler(int sig)
{
	//Au cas où l'on aurait envie de traiter ce signal.
}

void * threadJoueursConnectes(void *p)
{
	struct sigaction sigact;

	sigact.sa_flags = 0;

	// Armement de SIGHUP
	sigact.sa_handler = sigHupHandler;
	sigaction(SIGHUP, &sigact, NULL);

	sigemptyset(&sigact.sa_mask);
	sigaddset(&sigact.sa_mask, SIGHUP);
	pthread_sigmask(SIG_UNBLOCK, &sigact.sa_mask, NULL);

	while(1)
	{
		pause(); // Attente de SIGHUP
	}
}

// Permet de rafraichir le nombre de joueurs connectés
void sigHupHandler(int sig)
{
	int nbJoueurs, i;

	nbJoueurs = GetNbJoueursConnectes(cleServer);
	if (nbJoueurs == -1) // Si le serveur envoie un signal et meurt juste après...
	{
		setMessage("Connexion au serveur perdue");
	}
	else
	{
		for (i = 0 ; i < 2 ; ++i)
		{
			DessineChiffre(12, 18-i, (int) (nbJoueurs/pow(10, i)) % 10); //Cette monstruosité permet d'obtenir le chiffre à l'indice i
		}
	}
}

void * threadTopScore(void *p)
{
	int i;
	TOPSCORE TopScore;
	struct sigaction sigact;

	// Armement de SIGQUIT
	sigact.sa_handler = sigQuitHandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGQUIT, &sigact, NULL);

	sigaddset(&sigact.sa_mask, SIGQUIT);
	pthread_sigmask(SIG_UNBLOCK, &sigact.sa_mask, NULL);

	// Affichage dU TopScore
	GetTopScore((key_t)cleServer, &TopScore);
	for (i = 0 ; i < 4 ; ++i)
	{
		DessineChiffre(8, 18-i, (int) (TopScore.score/pow(10, i)) % 10); //Cette monstruosité permet d'obtenir le chiffre à l'indice i
	}

	// Afin de voir le message de bienvenue défiler, on n'affichera le détenteur du topscore dans 30 sec
	sleep(30);

	pthread_kill(threadHandleTopScore, SIGQUIT);

	while(1)
	{
		pause(); // Attente de SIGQUIT
	}
}

void sigQuitHandler(int sig)
{	
	int i;
	char *TopJoueurM;
	TOPSCORE TopScore;

	GetTopScore((key_t)cleServer, &TopScore);
	for (i = 0 ; i < 4 ; ++i)
	{
		DessineChiffre(8, 18-i, (int) (TopScore.score/pow(10, i)) % 10); //Cette monstruosité permet d'obtenir le chiffre à l'indice i
	}

	TopJoueurM = (char *) malloc (strlen(TopScore.login) + strlen(TopScore.pseudo) + 2 ); // +2 pour un espace et un \n
	sprintf(TopJoueurM, "%s %s", TopScore.login, TopScore.pseudo);
	setMessage(TopJoueurM);
	free(TopJoueurM);
}

void terminerProgramme()
{
	int i, j;

	DessineSprite(12, 11, VOYANT_ROUGE);

	printf("Suppression du threadEvent...\n");
	if (pthread_cancel(threadHandleEvent) != 0)
		printf("Erreur de pthread_cancel\n");
	pthread_join(threadHandleEvent, NULL);

	printf("Suppression des Threads cases...\n");
	for (i = 0 ; i < NB_LIGNES ; ++i)
	{
		for (j = 0 ; j < 10 ; ++j)
		{
			pthread_cancel(threadHandleCase[i][j]);
		}
	}
	for (i = 0 ; i < NB_LIGNES ; ++i)
	{
		for (j = 0 ; j < 10 ; ++j)
		{
			pthread_join(threadHandleCase[i][j], NULL);
		}
	}

	printf("Supression du threadTopScore\n");
	pthread_cancel(threadHandleTopScore);
	pthread_join(threadHandleTopScore, NULL);

	printf("Supression du threadScore\n");
	pthread_cancel(threadHandleScore);
	pthread_join(threadHandleScore, NULL);

	printf("Attente du clic sur la croix avant de fermer la grille...\n");
	EVENT_GRILLE_SDL event;

	event = ReadEvent();
	while (event.type != CROIX)
	{
		event = ReadEvent();
		printf("Action recue du type: %d\n", event.type);
	}

	// Fermeture de la grille de jeu (SDL)
	printf("(THREAD EVENT) Fermeture de la grille...\n"); 
	fflush(stdout);
	FermerGrilleSDL();
	fflush(stdout);
	
	printf("Suppression du threadDefileMessage\n");
	pthread_cancel(threadHandleMessage);

	if (cleServer)
	{
		printf("Deconnection du serveur...\n");
		DeconnectionServeur((key_t)cleServer);
	}

	exit(0);
}
