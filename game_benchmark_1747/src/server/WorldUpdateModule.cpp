/***************************************************************************************************
 *
 * SUBJECT:
 *    A Benckmark for Massive Multiplayer Online Games
 *    Game Server and Client
 *
 * AUTHOR:
 *    Mihai Paslariu
 *    Politehnica University of Bucharest, Bucharest, Romania
 *    mihplaesu@yahoo.com
 *
 * TIME AND PLACE:
 *    University of Toronto, Toronto, Canada
 *    March - August 2007
 *
 ***************************************************************************************************/

#include "ServerData.h"
#include "WorldUpdateModule.h"

#include <fstream>
#include <cstdio>

/***************************************************************************************************
 *
 * Constructors and setup methods
 *
 ***************************************************************************************************/

WorldUpdateModule::WorldUpdateModule(int id, MessageModule *_comm,
		SDL_barrier *_barr) {
	assert(id >= 0 && id < sd->num_threads && _comm && _barr);
	t_id = id;
	barrier = _barr;

	comm = _comm;

	avg_wui = -1;
	avg_rui = -1;

	assert(SDL_CreateThread( module_thread, (void*)this ) != NULL);
}

/***************************************************************************************************
 *
 * Main loop
 *
 ***************************************************************************************************/

void WorldUpdateModule::run() {
	Uint32 start_time;
	Uint32 timeout;
	Uint32 wui, rui;

	Message *m;
	IPaddress addr;
	int type;

	Player *p;
	PlayerBucket* bucket = &sd->wm.players[t_id];

	Serializator *s = NULL;
	MessageWithSerializator *ms = NULL;

	Uint32 start_quest = SDL_GetTicks() + sd->quest_between;
	Uint32 end_quest = start_quest + sd->quest_min
			+ rand() % (sd->quest_max - sd->quest_min + 1);

	// Open file descriptors for each CSV file we should write. Write the headers.
	std::ofstream processing_file;
	char processing_filename[25];
	sprintf(processing_filename, "./processing_time/%d_processing_time.csv",
			t_id);
	processing_file.open(processing_filename,
			std::ofstream::out | std::ofstream::trunc);
	processing_file << "thread,iterations,processing_time,requests"
			<< std::endl;

	std::ofstream update_file;
	char update_filename[25];
	sprintf(update_filename, "./update_time/%d_update_time.csv", t_id);
	update_file.open(update_filename,
			std::ofstream::out | std::ofstream::trunc);
	update_file << "thread,iterations,update_time,updates" << std::endl;

	int iterations = 0;

	printf("WorldUpdateModule started\n");
	/* main loop */
	while (true) {
		iterations++;

		start_time = SDL_GetTicks();
		timeout = sd->regular_update_interval;

		int num_req = 0;
		int total_processing_time = 0;

		while ((m = comm->receive(timeout, t_id)) != NULL) {
			num_req++;
			int processing_start_time = SDL_GetTicks();

			addr = m->getAddress();
			type = m->getType();
			p = sd->wm.findPlayer(addr, t_id);

			switch (type) {
			case MESSAGE_CS_JOIN:
				handleClientJoinRequest(p, addr);
				break;
			case MESSAGE_CS_LEAVE:
				handleClientLeaveRequest(p);
				break;

			case MESSAGE_CS_MOVE_DOWN:				// dir 0
			case MESSAGE_CS_MOVE_RIGHT:				// dir 1
			case MESSAGE_CS_MOVE_UP:				// dir 2
			case MESSAGE_CS_MOVE_LEFT:				// dir 3
				handle_move(p, type - MESSAGE_CS_MOVE_DOWN);
				break;

			case MESSAGE_CS_ATTACK_DOWN:			// dir 0
			case MESSAGE_CS_ATTACK_RIGHT:			// dir 1
			case MESSAGE_CS_ATTACK_UP:				// dir 2
			case MESSAGE_CS_ATTACK_LEFT:			// dir 3
				sd->wm.attackPlayer(p, type - MESSAGE_CS_ATTACK_DOWN);
				break;
			case MESSAGE_CS_USE:
				sd->wm.useGameObject(p);
				break;

			default:
				printf(
						"[WARNING] Unknown message (%d) received from %u.%u.%u.%u:%d\n",
						m->getType(), addr.host & 0xFF, (addr.host >> 8) & 0xFF,
						(addr.host >> 16) & 0xFF, addr.host >> 24, addr.port);
			}

			delete m;
			timeout = sd->regular_update_interval
					- (SDL_GetTicks() - start_time);
			if (((int) timeout) < 0)
				timeout = 0;

			total_processing_time += SDL_GetTicks() - processing_start_time;
		}

		SDL_WaitBarrier(barrier);

		processing_file << t_id << "," << iterations << ","
				<< total_processing_time << "," << num_req << endl;


		if (t_id == 0) {
			sd->wm.balance();

			if (rand() % 100 < 10)
				sd->wm.regenerateObjects();

			sd->send_start_quest = 0;
			sd->send_end_quest = 0;
			if (start_time > start_quest) {
				start_quest = end_quest + sd->quest_between;
				// Always set the quest to occur at (42,59) so that experimental runs are consistent
				sd->quest_pos.x = 42;
				sd->quest_pos.y = 59;
				// sd->quest_pos.x = (rand() % sd->wm.n_regs.x) * CLIENT_MATRIX_SIZE + MAX_CLIENT_VIEW;
				// sd->quest_pos.y = (rand() % sd->wm.n_regs.y) * CLIENT_MATRIX_SIZE + MAX_CLIENT_VIEW;
				sd->send_start_quest = 1;
				if (sd->display_quests)
					printf("New quest %d,%d\n", sd->quest_pos.x,
							sd->quest_pos.y);
			}
			if (start_time > end_quest) {
				sd->wm.rewardPlayers(sd->quest_pos);
				end_quest = start_quest + sd->quest_min
						+ rand() % (sd->quest_max - sd->quest_min + 1);
				sd->send_end_quest = 1;
				if (sd->display_quests)
					printf("Quest over\n");
			}
		}

		SDL_WaitBarrier(barrier);

		wui = SDL_GetTicks() - start_time;
		avg_wui = (avg_wui < 0) ? wui : (avg_wui * 0.95 + (double) wui * 0.05);
		start_time = SDL_GetTicks();

		int num_updates = 0;
		int total_update_time = 0;

		/* send updates to clients (map state) */
		bucket->start();
		while ((p = bucket->next()) != NULL) {
			num_updates++;
			int update_start_time = SDL_GetTicks();

			ms = new MessageWithSerializator(MESSAGE_SC_REGULAR_UPDATE, t_id,
					p->address);
			assert(ms);
			s = ms->getSerializator();
			assert(s);

			sd->wm.updatePlayer(p, s);

			ms->prepare();
			comm->send(ms, t_id);

			if (sd->send_start_quest)
				comm->send(
						new MessageXY(MESSAGE_SC_NEW_QUEST, t_id, p->address,
								sd->quest_pos), t_id);
			if (sd->send_end_quest)
				comm->send(new Message(MESSAGE_SC_QUEST_OVER, t_id, p->address),
						t_id);

			total_update_time += SDL_GetTicks() - update_start_time;
		}

		SDL_WaitBarrier(barrier);
		rui = SDL_GetTicks() - start_time;
		avg_rui = (avg_rui < 0) ? rui : (avg_rui * 0.95 + (double) rui * 0.05);

		update_file << t_id << "," << iterations << "," << total_update_time
				<< "," << num_updates << endl;
	}

	// Close any open files
	processing_file.close();
	update_file.close();
}

/***************************************************************************************************
 *
 * Handle client requests
 *
 ***************************************************************************************************/

/* generate a new player, send an ok message */
void WorldUpdateModule::handleClientJoinRequest(Player* p, IPaddress addr) {
	if (p) {
		comm->send(new Message(MESSAGE_SC_NOK_JOIN, 0, p->address), t_id);
		printf(
				"[WARNING] Player already on server '%s' (send not ok to join message)\n",
				p->name);
		return;
	}

	p = sd->wm.addPlayer(addr);

	MessageOkJoin *mok = new MessageOkJoin(t_id, p->address, p->name, p->pos,
			sd->wm.size);
	comm->send((Message*) mok, t_id);

	if (sd->display_user_on_off)
		printf("New player: %s (%d,%d)\n", p->name, p->pos.x, p->pos.y);
}

/* remove client from WorldMap and send an ok_leave message */
void WorldUpdateModule::handleClientLeaveRequest(Player* p) {
	assert(p);
	sd->wm.removePlayer(p);

	Message *mok = new Message(MESSAGE_SC_OK_LEAVE, t_id, p->address);
	comm->send(mok, t_id);

	if (sd->display_user_on_off)
		printf("Removing player %s\n", p->name);
	delete p;
}

void WorldUpdateModule::handle_move(Player* p, int _dir) {
	assert(p);
	p->dir = _dir;
	sd->wm.movePlayer(p);
}

