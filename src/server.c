/*
 * Projet Unix 2012 : Code de Vinci
 *
 * Partie serveur
 */
#include "server.h"

SOCKET conn; /* Socket de connection */
char *shm_addr; /* Adresse de la mémoire partagée */
char *rdr_addr; /* Adresse de la mémoire partagée */
int num_players = 0; /* Nombre de joueurs */
int pid = 1; /* Process id après fork */
int shm_id = 0; /* identifieur de la mémoire partagée */
int rdr_id = 0; /* Identifieur du nombre de lecteur (shm) */
int sem_id = 0; /* Identifieur du set de 2 sémaphores */
int port; /* Port utilisé pour le socket de connexion */
int failed = FALSE; /* Pour savoir si le dernier coup était un coup raté ou  non */
int num_playing; /* Nombre de joueurs encore entrain de joueur (n'ayant pas perdu ou n'étant pas déconnectés */
UserInfo users[MAX_PLAYERS]; /* Tableau des joueurs (pipes + pseudo + score + ...) */
int started = FALSE; /* Indique si la partie est en cours ou non */
GameBoard board; /* Le tableau de jeu */
Deck pick_deck; /* La pioche */
SharedMem shared_mem; /* Qui sert pour stocker en mémoire partagée le tableau de jeu */
int start_counter; /* Compteur pour savoir combien de joueurs ont déjà renvoyés leurs pièces */
int who_play; /* Permet de connaitre le numéro du joueur entrain de jouer */
Message last_attack; /* Pour garder la dernière attaque reçue */

/* Méthode pour initialisé les handlers des signaux et alarmes,
 * sémaphores, mémoires partagées, ... */
void init(void)
{
	/* On initialise la seed pour les rand() */
	srand(time(NULL));

	/* Handlers de signaux et alarmes */
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);

	/* Pour l'alarme */
	sa.sa_handler = alarm_handler;
	sa.sa_flags = SA_RESTART;
	SYS(sigaction(SIGALRM, &sa, NULL));

	/* Pour un fils qui se termine */
	sa.sa_handler = sigchld_handler;
	SYS(sigaction(SIGCHLD, &sa, NULL));

	/* Pour l'interruption */
	sa.sa_handler = termination_handler;
	sa.sa_flags = 0;
	SYS(sigaction(SIGINT, &sa, NULL));

	/* Création du nombre de lecteur en mémoire partagée (Courtois) */
	create_shared_readers();

	/* Création des sémaphores */
	create_semaphore();
}

/* Handler pour capter la fin d'un fils et ainsi éviter des fils zombie */
void sigchld_handler(int signum)
{
	int status;
	wait(&status);
}

/* Handler pour l'alarme permettant de gérer le temps d'inscription */
void alarm_handler(int signum)
{
	/* si - de 2 jours on stop
	 * si 2 ou 3 jours ok on lance la partie
	 * si 4 joueur on ne passe pas ici car l'alarme sera arrêtée */
	if (num_players < 2)
	{
		/* Partie annulée */
		int i;
		for (i = 0; i < num_players; i++)
		{
			Message m =
			{ 0 };
			m.msgString.type = 1;
			sprintf(m.msgString.msg, "La partie est annulée car pas assez de joueurs.");
			SYS(write(users[i].fd_w, &m, sizeof(m)));

			SYS(close(users[i].fd_r));
			SYS(close(users[i].fd_w));
		}
		num_players = 0;
	}
	else
	{
		/* Sinon on commence la partie */
		start_game();
	}
}

/* Méthode pour démarrer la partie, distribuer les pièces, ... */
void start_game(void)
{
	int i, j, x;
	int num_tokens = 4;

	/* On arrête l'alarme */
	alarm(0);

	/* On remplit la pioche avec les 24 pièces de départ */
	pick_deck.length = NUM_TOKENS;

	for (i = 0; i < (pick_deck.length / 2); i++)
	{
		pick_deck.tokens[i].num = i;
		pick_deck.tokens[i].color = BLACK;
		pick_deck.tokens[i].hidden = TRUE;

		pick_deck.tokens[i + (pick_deck.length / 2)].num = i;
		pick_deck.tokens[i + (pick_deck.length / 2)].color = WHITE;
		pick_deck.tokens[i + (pick_deck.length / 2)].hidden = TRUE;
	}

	/* On mélange la pioche */
	for (i = 0; i < pick_deck.length; i++)
	{
		x = rand() % pick_deck.length;
		Token tmp = pick_deck.tokens[i];
		pick_deck.tokens[i] = pick_deck.tokens[x];
		pick_deck.tokens[x] = tmp;
	}

	/* Que 3 pièces si il y a 4 joueurs ! */
	if (num_players == 4)
	{
		num_tokens = 3;
	}

	/* On garde le nombre de pieces qu'il reste a découvrir pour chaque joueur */
	for (i = 0; i < num_players; i++)
	{
		users[i].hidden_tokens = num_tokens;
	}

	/* Variable globales pour la gestion du jeu et des tours */
	num_playing = num_players;
	who_play = -1;
	start_counter = 0;
	started = TRUE;

	/* Création du plateau de jeu */
	for (i = 0; i < num_players; i++)
	{
		/* On distribue à chaque joueur 4 pièces au début */
		for (j = 0; j < num_tokens; j++)
		{
			board.decks[i].tokens[j] = pick_deck.tokens[--pick_deck.length];
		}
		board.decks[i].length = num_tokens;
	}
	board.length = num_players;

	/* On crée la mémoire partagée si elle n'existait pas */
	create_shared_mem();

	/* On met à jour le plateau de jeux dans la mémoire partagée */
	update_shared_mem();

	/* On envoie les pièces non triées des joueurs à chacun des fils */
	for (i = 0; i < num_players; i++)
	{
		Message m =
		{ 0 };
		m.msgDeck.type = MSG_INIT_DECK;
		m.msgDeck.deck = board.decks[i];

		SYS(write(users[i].fd_w, &m, sizeof(m)));
	}
}

/* Méthode pour créer ou récupérer la mémoire partagée du tableau de jeu si elle existe déjà */
void create_shared_mem(void)
{
	/* On crée la mémoire partagée pour le tableau de jeu */
	if (shm_id == 0)
	{
		/* On change le droit d'écriture/lecture selon le pid (père ou fils) */
		SYS(shm_id = shmget(SHM_KEY_1, sizeof(SharedMem), (pid ? 0666 : 0444) | IPC_CREAT));

		/* On l'attache à la mémoire */
		if ((shm_addr = shmat(shm_id, NULL, 0)) == (void*) -1)
		{
			perror("shmat()");
			exit(EXIT_FAILURE);
		}
	}
}

/* Méthode pour créer ou récupérer la mémoire partagée du nombre de lecteurs (courtois) */
void create_shared_readers(void)
{
	// On crée la mémoire partagée pour le reader
	if (rdr_id == 0)
	{
		// On change le droit d'écriture/lecture selon le pid (fils ou père?)
		SYS(rdr_id = shmget(SHM_KEY_2, sizeof(unsigned int), (pid ? 0666 : 0444) | IPC_CREAT));

		// On l'attache à la mémoire
		if ((rdr_addr = shmat(rdr_id, NULL, 0)) == (void*) -1)
		{
			perror("shmat()");
			exit(EXIT_FAILURE);
		}

		*((unsigned int*) rdr_addr) = 0;
	}
}

/* Méthode pour créer le set de 2 sémaphores */
void create_semaphore(void)
{
	if (sem_id == 0)
	{
		/* On crée un set de deux sémaphores */
		SYS(sem_id = semget(SEM_KEY, 2, 0666 | IPC_CREAT));

		/* On initialise les deux valeurs du set de sémaphore à 1 car
		 * selon le man, POSIX ne spécifie pas à quelle valeur ils sont
		 * initialisés ! */
		SYS(semctl(sem_id, SEM_MUTEX, SETVAL, 1));
		SYS(semctl(sem_id, SEM_SHARED, SETVAL, 1));
	}
}

/* Méthode pour mettre le plateau de jeu dans la mémoire partagée */
void update_shared_mem(void)
{
	int i, j;

	shared_mem.board = board;

	/* On cache les pièces cachées dans la variable servant pour la mémoire partagée */
	for (i = 0; i < num_players; i++)
	{
		for (j = 0; j < shared_mem.board.decks[i].length; j++)
		{
			if (shared_mem.board.decks[i].tokens[j].hidden)
			{
				shared_mem.board.decks[i].tokens[j].num = HIDDEN;
			}
		}
	}

	/* Puis on envoie cette variable dans la mémoire partagée
	 * Courtois : rédacteur */
	down(SEM_SHARED);
	*((SharedMem*) shm_addr) = shared_mem;
	up(SEM_SHARED);
}

/* Méthode down sur un numéro de sémaphore dans le set */
void down(int sem_num)
{
	struct sembuf sops;
	sops.sem_num = sem_num;
	sops.sem_flg = SEM_UNDO;
	sops.sem_op = -1;
	SYS(semop(sem_id, &sops, 1));
}

/* Méthode up sur un numéro de sémaphore dans le set existant */
void up(int sem_num)
{
	struct sembuf sops;
	sops.sem_num = sem_num;
	sops.sem_flg = SEM_UNDO;
	sops.sem_op = 1;
	SYS(semop(sem_id, &sops, 1));
}

/* Méthode pour renvoyer à chacun des fils les pièces de son joueur et ainsi
 * indiquer que la mémoire partagée contenant le plateau de jeu a été mise à jour
 */
void notify_updated(void)
{
	int i;

	if (started)
	{
		for (i = 0; i < num_players; i++)
		{
			if (users[i].connected)
			{
				Message m =
				{ 0 };
				m.msgDeck.type = MSG_DECK;
				m.msgDeck.deck = board.decks[i];
				SYS(write(users[i].fd_w, &m, sizeof(m)));
			}
		}
	}
}

/* Handler pour gérer la fin du programme (CTRL+C) */
void termination_handler(int signum)
{
	end();
	exit(EXIT_SUCCESS);
}

/* Méthode pour nettoyer et fermer ce qui a été ouvert avant de fermer le programme */
void end(void)
{
	/* Fin du père */
	if (pid)
	{
		/* On ferme le socket de connection */
		SYS(close(conn));

		/* On détache et supprime la mémoire partagée du plateau si elle existait */
		if (shm_id != 0)
		{
			SYS(shmdt(shm_addr));
			SYS(shmctl(shm_id, IPC_RMID, NULL));
		}

		/* On détache et supprime la mémoire partagée des lecteurs si elle existait */
		if (rdr_id != 0)
		{
			SYS(shmdt(rdr_addr));
			SYS(shmctl(rdr_id, IPC_RMID, NULL));
		}

		/* On supprime le set de sémaphores s'il existait */
		if (sem_id != 0)
		{
			SYS(semctl(sem_id, 0, IPC_RMID, NULL));
		}
	}
	/* Fin du fils */
	else
	{
		/* On détache ce processus de la mémoire partagée du plateau */
		if (shm_id != 0)
		{
			SYS(shmdt(shm_addr));
		}

		/* On détache ce processus de la mémoire partagée des lecteurs */
		if (rdr_id != 0)
		{
			SYS(shmdt(rdr_addr));
		}
	}
}

/* Méthode principale contenant la boucle du jeu */
void app(void)
{
	int max_fd; /* Plus grand FD pour le select */

	int fd_a[2]; /* pour le pipe 1 (aller) */
	int fd_b[2]; /* pour le pipe 2 (retour) */

	fd_set rfds; /* Ensemble de FD pour le select */

	int i, n;
	SOCKET csock; /* Socket d'un client */

	conn = init_connection(); /* Socket de connexion */

	/* Boucle principale du serveur (père) */
	while (TRUE)
	{
		/* on vide le fds */
		FD_ZERO(&rfds);

		/* On ajoute le socket de connexion */
		FD_SET(conn, &rfds);
		max_fd = conn;

		/* On ajoute les pipes des fils de joueurs connectés */
		for (i = 0; i < num_players; i++)
		{
			if (!users[i].connected || !users[i].playing)
			{
				/* On n'écoute pas les joueurs non connectés ou qui ont perdus */
				continue;
			}

			FD_SET(users[i].fd_r, &rfds);

			/* On a besoin du plus grand FD pour le select */
			if (users[i].fd_r > max_fd)
			{
				max_fd = users[i].fd_r;
			}
		}

		/* On attend de recevoir quelque chose sur un des FD du rdfs */
		if ((n = select(max_fd + 1, &rfds, NULL, NULL, NULL)) == -1)
		{
			/* Si ce n'est pas une interruption (alarme), ce n'est pas normal */
			if (errno != EINTR)
			{
				perror("select()");
				exit(EXIT_FAILURE);
			}

			/* Si on a eu une interruption, il faut redémarrer la boucle sinon
			 * le rfds n'est pas mis à jour et le programme va bloquer sur le accept */
			continue;
		}

		/* Vérifions que c'est bien le FD du socket de connexion qui a fait sortir du select */
		if (FD_ISSET(conn, &rfds))
		{
			/* structure pour stocker les informations du client qui se connecte */
			SOCKADDR_IN csin;

			socklen_t addrlen = sizeof csin;
			SYS(csock = accept(conn, (SOCKADDR *) &csin, &addrlen));

			/* Si la partie a déjà commencé, on le dit au client que la partie est en cours */
			if (started)
			{
				Message m;
				m.msgString.type = MSG_STARTED;
				sprintf(m.msgString.msg, "La partie a déjà commencé... au revoir !");
				SYS(write(csock, &m, sizeof(m)));
				continue;
			}

			/* Pipes pour dialoguer avec le fils */
			SYS(pipe(fd_a));
			SYS(pipe(fd_b));

			/* Fork pour créer le fils pour ce joueur */
			SYS(pid = fork());

			if (pid)
			{
				/* Père */

				/* Lancement de l'alarme avec la connexion du premier joueur */
				if (num_players == 0)
				{
					alarm(ALRM_TIME);
				}

				/* 1er pipe pour écrire, 2e pour lire */
				SYS(close(fd_a[0]));
				SYS(close(fd_b[1]));

				/* On sauvegarde les sockets d'écriture et lecture */
				users[num_players].fd_w = fd_a[1];
				users[num_players].fd_r = fd_b[0];

				/* Initialisations de base */
				users[num_players].connected = TRUE;
				users[num_players].playing = TRUE;
				users[num_players].score = 0;
				num_players++;

				/* On ferme le socket avec le joueur chez le père */
				SYS(close(csock));

				/* On démarre la partie si on a 4 joueurs */
				if (num_players == MAX_PLAYERS)
				{
					start_game();
				}
			}
			else
			{
				/* Fils */

				/* On ferme le socket de connexion chez le fils */
				SYS(close(conn));

				/* 1er pipe pour lire et 2e pour écrire */
				SYS(close(fd_a[1]));
				SYS(close(fd_b[0]));

				/* Boucle principale du fils */
				while (TRUE)
				{
					/* On vide le fds au cas où il resterai qqch en mémoire à cet endroit */
					FD_ZERO(&rfds);

					/* On ajoute le socket du client et le pipe de lecture */
					FD_SET(csock, &rfds);
					FD_SET(fd_a[0], &rfds);

					/* On compare les deux FD pour trouver le plus grand */
					max_fd = (csock > fd_a[0] ? csock : fd_a[0]);

					/* On attend de recevoir quelque chose sur un des FD de rdfs */
					SYS(n = select(max_fd + 1, &rfds, NULL, NULL, NULL));

					if (FD_ISSET(csock, &rfds))
					{
						/* On reçoit quelque chose sur le socket du client */
						Message m;
						SYS(n = read(csock, &m, sizeof(m)));

						if (!n)
						{
							/* Fin de la connexion de ce client */
							SYS(close(csock));
							SYS(close(fd_a[0]));
							SYS(close(fd_b[1]));
							end();
							exit(EXIT_SUCCESS);
						}

						/* Le switch permettrait de traiter différement certains types de messages.
						 * Mais dans ce sens ce n'est pour l'instant pas le cas
						 */
						switch (m.type)
						{
						default:
							/* on transmet au père ce qu'on a reçu du joueur */
							SYS(write(fd_b[1], &m, sizeof(m)));
							break;
						}

					}
					else if (FD_ISSET(fd_a[0], &rfds))
					{
						/* On reçoit quelque chose sur le pipe de lecture venant du père */
						Message m1, m2;
						SYS(n = read(fd_a[0], &m1, sizeof(m1)));

						if (!n)
						{
							/* Si le père ferme le pipe, on ferme le socket du client */
							close(csock);
							end();
							exit(EXIT_FAILURE);
						}

						/* Les messages reçus du père sont traités selon leur type */
						switch (m1.type)
						{
						case MSG_DECK: /* Deck des pièces du joueur */
							/* On transmet le deck reçu du père au client */
							SYS(write(csock, &m1, sizeof(m1)));

							/* On récupère le plateau dans la mémoire partagée et on l'envoie au client */
							m2.msgBoard.type = MSG_BOARD;
							m2.msgBoard.board = get_shared_board();
							SYS(write(csock, &m2, sizeof(m2)));
							break;
						case MSG_INIT_DECK: /* Pièces non triées envoyées en début de partie */
							/* On s'attache à la mémoire partagée */
							create_shared_mem();

							/* On envoie le message "La partie démarre" à son joueur */
							m2.msgString.type = MSG_STRING;
							sprintf(m2.msgString.msg, "La partie démarre");
							SYS(write(csock, &m2, sizeof(m2)));

							/* Et on lui envoie aussi ses pièces pour qu'il les tries */
							SYS(write(csock, &m1, sizeof(m1)));
							break;
						default:
							/* Par défaut si le message n'est pas d'un type ci-dessus, on le transmet simplement */
							SYS(write(csock, &m1, sizeof(m1)));
							break;
						}
					}
				}
			}
		}

		/* Si le processus père reçoit quelque chose sur un des pipes des joueurs (et non le socket de connexion) */
		if (n > 0 && !FD_ISSET(conn, &rfds))
		{
			for (i = 0; i < num_players; i++)
			{
				/* On cherche quel joueur à parler et on traite ce qu'on a reçu */
				if (FD_ISSET(users[i].fd_r, &rfds))
				{
					int j;
					int user, pos, card_num, source, length_tmp;
					Deck deck_tmp;

					Message m; /* Message pour lire ce qui est reçu */
					Message m_pos; /* Message pour demander la position de la pièce courante */
					Message m_str; /* Message pour envoyer du texte */

					SYS(n = read(users[i].fd_r, &m, sizeof(m)));

					/* Gestion de la déconnexion */
					if (!n)
					{
						fprintf(stderr, "Fin de la connexion du joueur %d-%s\n", i, users[i].username);

						/* On supprime le user de la table si on était à l'inscription
						 * sinon on le marque comme étant déconnecté pour conserver ses infos */
						if (started)
						{
							Message m_dec;

							/* On marque le joueur comme non connecté et ne jouant plus */
							users[i].connected = FALSE;
							users[i].playing = FALSE;
							num_playing--;

							/* On dévoile toutes les pièces du joueur */
							for (j = 0; j < board.decks[i].length; j++)
							{
								board.decks[i].tokens[j].hidden = FALSE;
							}

							/* On indique aux joueurs qu'il s'est déconnecté */
							m_dec.msgLogout.type = MSG_LOGOUT;
							m_dec.msgLogout.player_id = i;
							send_to_all(m_dec);

							/* S'il ne reste plus qu'un seul joueur, c'est le grand gagnant ! */
							if (num_playing == 1)
							{
								m_dec.msgString.type = MSG_STRING;
								strcpy(m_dec.msgString.msg, "Vous êtes le gagnant car il n'y a plus d'autres joueurs !");

								for (j = 0; j < num_players; j++)
								{
									if (users[j].connected && users[j].playing)
									{
										SYS(write(users[j].fd_w, &m_dec, sizeof(m_dec)));
									}
								}
								end_game();
								continue;
							}

							/* On remet la pièce dans la pioche et on relance le tour */
							pick_deck.length++;
							who_play--;
							do_turn();
						}
						else
						{
							/* Si la partie n'avais pas commencer, on supprime le joueur de la table */
							delete_player(i);
						}
						continue;
					}

					/* On traite différement selon le type du message reçu */
					switch (m.type)
					{
					case MSG_POS: /* Message indiquant la position de la pièce courante */
						source = last_attack.msgAttack.source;
						length_tmp = board.decks[source].length + 1;
						j = 0;

						if (failed)
						{
							/* Attaque ratée : on dévoile la piéce dans le jeu de l'attaquant */
							m.msgPos.t.hidden = FALSE;
							failed = FALSE;
						}
						else
						{
							/* Attaque réussie : le joueur a un token de caché en plus dans son jeu */
							users[last_attack.msgAttack.source].hidden_tokens++;
						}

						/* On place la pièce à la position reçue du joueur */
						for (i = 0; i < length_tmp; i++)
						{
							if (i == m.msgPos.pos)
							{
								deck_tmp.tokens[i] = m.msgPos.t;
							}
							else
							{
								deck_tmp.tokens[i] = board.decks[source].tokens[j];
								j++;
							}
						}

						deck_tmp.length = length_tmp;
						board.decks[source] = deck_tmp;

						/* Mise à jour de la mémoire partagée, envoi des pièces et prochain tour */
						update_shared_mem();
						notify_updated();
						do_turn();
						break;
					case MSG_OKKO: /* Message contenant la réponse du joueur concernant l'attaque */
						m_str.msgString.type = MSG_STRING;

						if (!strcmp(m.msgString.msg, CONFIRM_OK))
						{
							/* Attaque réussie : on dévoile la pièce dans le jeu de l'attaqué */
							board.decks[last_attack.msgAttack.target].tokens[last_attack.msgAttack.pos].hidden = FALSE;

							/* On retire 1 au nombre de pièces cachée qu'il reste au joueur attaqué */
							users[last_attack.msgAttack.target].hidden_tokens--;

							/* Une attaque gagnante simple rapporte 1 point */
							if (users[last_attack.msgAttack.target].hidden_tokens > 0)
							{
								users[last_attack.msgAttack.source].score++;
							}
							/* Une attaque gagnante revelant le code de l'attaque rapporte 3 point */
							else if (users[last_attack.msgAttack.target].hidden_tokens == 0)
							{
								num_playing--;
								users[last_attack.msgAttack.target].playing = FALSE;
								users[last_attack.msgAttack.source].score += 3;

								/* S'il ne reste plus qu'une personne dont toutes les pièces ne sont pas dévoilées */
								if (num_playing == 1)
								{
									/* Il reçoit 5 points et 1 point par pièce non dévoilée */
									users[last_attack.msgAttack.source].score += 5;
									users[last_attack.msgAttack.source].score += users[last_attack.msgAttack.source].hidden_tokens;

									strcpy(m_str.msgString.msg, "Vous avez gagné, félicitations !");
									SYS(write(users[last_attack.msgAttack.source].fd_w, &m_str, sizeof(m_str)));
									end_game(); /* fin de la partie et affichage des scores */
									continue;
								}
							}

							/* Message d'information pour tout le monde */
							sprintf(m_str.msgString.msg, "%s a réussi son attaque contre %s !",
									users[last_attack.msgAttack.source].username, users[last_attack.msgAttack.target].username);
						}
						else
						{
							/* Attaque ratée (KO) : on dévoile la pièce dans le jeu de l'attaquant */
							failed = TRUE;
							sprintf(m_str.msgString.msg, "%s a raté son attaque contre %s !", users[last_attack.msgAttack.source].username,
									users[last_attack.msgAttack.target].username);
						}

						/* On envoie le message réussi ou raté à tous */
						send_to_all(m_str);

						if (pick_deck.length > 0)
						{
							/* Demander la position de la pièce courante au joueur attaquant */
							Message m_pos;
							m_pos.type = MSG_POS;
							SYS(write(users[last_attack.msgAttack.source].fd_w, &m_pos, sizeof(m_pos)));
						}
						else
						{
							/* S'il n'y a plus de pièces dans la pioche on passe au tour suivant */
							update_shared_mem();
							notify_updated();
							do_turn();
						}
						break;
					case MSG_ATTACK: /* Message contenant l'attaque d'un joueur envers un autre */
						/* On garde la dernière attaque ! */
						last_attack = m;

						/* Et on la transmet à tous les fils */
						send_to_all(m);
						break;
					case MSG_LOGIN: /* Message contenant le login/pseudo du joueur */
						strcpy(users[i].username, m.msgString.msg);
						strcpy(board.id[i], m.msgString.msg);
						break;
					case MSG_INIT_DECK: /* Message contenant le deck trié par le joueur */
						board.decks[i] = m.msgDeck.deck;

						/* On attends d'avoir reçu le deck de chaque joueur avant de commencer la partie */
						if (++start_counter == num_players)
						{
							update_shared_mem();
							notify_updated();
							do_turn(); // lancement du premier tour
						}
						break;
					}
				}
			}
		}
	}
}

/* Lecteur de courtois pour récupérer le plateau de jeu dans la mémoire partagée */
GameBoard get_shared_board(void)
{
	GameBoard board;

	down(SEM_MUTEX);
	*((unsigned int*) rdr_addr) += 1;
	if (*((unsigned int*) rdr_addr) == 1)
	{
		down(SEM_SHARED);
	}
	up(SEM_MUTEX);

	/* On lit la mémoire partagée */
	board = ((SharedMem*) shm_addr)->board;

	down(SEM_MUTEX);
	*((unsigned int*) rdr_addr) -= 1;
	if (*((unsigned int*) rdr_addr) == 0)
	{
		up(SEM_SHARED);
	}
	up(SEM_MUTEX);

	return board;
}

/* Méthode pour lancer un nouveau tour de la partie */
void do_turn(void)
{
	Message m;
	who_play = ++who_play % num_players;

	/* Si l'utilisateur en cours n'est plus connecté ou qu'il ne joue plus, on saute au suivant tour */
	if (!users[who_play].connected || !users[who_play].playing)
	{
		do_turn();
		return;
	}

	/* On envoie une pièce au joueur dont c'est le tour s'il en reste dans la pioche */
	m.msgOneToken.type = MSG_TOKEN;
	if (pick_deck.length > 0)
	{
		/* On prend la dernière pièce de la pioche */
		m.msgOneToken.t = pick_deck.tokens[--pick_deck.length];
	}
	else
	{
		/* Pièce vide car pioche vide */
		m.msgOneToken.t.num = -1;
	}

	SYS(write(users[who_play].fd_w, &m, sizeof(m)));
}

/* On supprime un joueur qui s'est déconnecté durant la phase d'inscription */
void delete_player(int i)
{
	int j;

	if (i >= num_players)
	{
		return;
	}

	num_players--;

	/* Si il n'y a plus de joueurs inscrits, on coupe l'alarme */
	if (num_players == 0)
	{
		alarm(0);
	}

	/* Si i est le dernier on décrémente juste le nombre de joueurs inscrits */
	if (i == num_players)
	{
		return;
	}

	/* On décale les joueurs suivants pour garder l'ordre d'inscription */
	for (j = i; j < num_players; j++)
	{
		users[j] = users[j + 1];
	}
}

/* Fin de la partie : affichage du score, réinitialisation pour prochaine partie, ... */
void end_game(void)
{
	Message m;
	int i;

	m.msgString.type = MSG_STRING;
	strcpy(m.msgString.msg, "Fin de la partie, voici les scores :\n");

	/* On crée le message contenant les scores et on l'envoie à tous */
	for (i = 0; i < num_players; i++)
	{
		sprintf(m.msgString.msg, "%s\t%s : %d\n", m.msgString.msg, users[i].username, users[i].score);
	}
	send_to_all(m);

	/* On ferme les pipes vers les fils pour terminé la partie */
	for (i = 0; i < num_players; i++)
	{
		SYS(close(users[i].fd_w));
		SYS(close(users[i].fd_r));
	}

	/* On réinitialise tout pour pouvoir redémarrer une nouvelle partie */
	started = FALSE;
	num_players = 0;
	board.length = 0;
	pick_deck.length = 0;
}

/* Fonction permettant de créer le socket de connexion */
SOCKET init_connection(void)
{
	int optval = 1; /* Pour le setsockopt() */

	SOCKET sock;
	SYS(sock = socket(AF_INET, SOCK_STREAM, 0));

	SOCKADDR_IN sin;

	/* On ajoute les informations à la structure 'sin' */
	sin.sin_addr.s_addr = htonl(INADDR_ANY); /* Adresse IP automatique */
	sin.sin_port = htons(port); /*Listage du port */
	sin.sin_family = AF_INET; /* Protocole familial (IP) */

	/* Autorise la réutilisation de l'adresse locale pour éviter le message
	 * "bind() : Adress already in use" */
	SYS(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)));

	if (bind(sock, (SOCKADDR *) &sin, sizeof sin) == -1)
	{
		if (errno == EADDRINUSE)
		{
			fprintf(stderr, "Un serveur est déjà lancé sur ce port\n");
		}
		else
		{
			perror("bind()");
		}
		exit(EXIT_FAILURE);
	}

	SYS(listen(sock, MAX_PLAYERS));

	return sock;
}

/* Fonction pour envoyer un message à tous les joueurs connectés */
void send_to_all(Message m)
{
	int i;

	for (i = 0; i < num_players; i++)
	{
		if (!users[i].connected)
		{
			/* On n'envoie pas le message aux joueurs déconnectés */
			continue;
		}

		SYS(write(users[i].fd_w, &m, sizeof(m)));
	}
}

/* Point d'entrée du programme */
int main(int argc, const char ** argv)
{
	if (argc < 2 || argc > 3)
	{
		fprintf(stderr, "Usage : %s port [err_out]\n", argv[0]);
		return EXIT_FAILURE;
	}

	port = atoi(argv[1]); /* Port utilisé pour le socket de connexion */

	/* Si la paramètre est présent, on redirige la sortie d'erreur vers le fichier spécifié */
	if (argc == 3)
	{
		int fd;
		SYS(fd = open(argv[2], O_RDWR | O_CREAT));
		SYS(dup2(fd, STDERR_FILENO));
	}

	/* Initialisation */
	init();

	/* Boucle de jeu principale */
	app();

	/* Fin du programme et nettoyage */
	end();

	return EXIT_SUCCESS;
}
