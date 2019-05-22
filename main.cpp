﻿/****************************************************
	Wirtualne zespoly robocze - przykladowy projekt w C++
	Do zadań dotyczących współpracy, ekstrapolacji i
	autonomicznych obiektów
	****************************************************/

#include <windows.h>
#include <math.h>
#include <time.h>

#include <gl\gl.h>
#include <gl\glu.h>
#include <iterator> 
#include <map>
#include <chrono>

using namespace std;

#include "objects.h"
#include "agents.h"
#include "graphics.h"
#include "net.h"
#include "main.h"


bool if_different_skills = false;          // czy zróżnicowanie umiejętności (dla każdego pojazdu losowane są umiejętności
// zbierania gotówki i paliwa)
bool if_autonomous_control = false;       // sterowanie autonomiczne pojazdem


FILE *f = fopen("vct_log.txt", "w");     // plik do zapisu informacji testowych

MovableObject *my_vehicle;             // Object przypisany do tej aplikacji
//MovableObject *MyOwnObjects[1000];        // obiekty przypisane do tej aplikacji max. 1000 (mogą być zarówno sterowalne, jak i poza kontrolą lub wrogie)
//int iNumberOfOwnObjects = 5;
//int iAkt = 0;                            // numer obiektu spośród przypisanych do tej aplikacji, który jest w danym momencie aktywny - sterowalny

Terrain terrain;
map<int, MovableObject*> network_vehicles;

AutoPilot *ap;

float fDt;                          // sredni czas pomiedzy dwoma kolejnymi cyklami symulacji i wyswietlania
long VW_cycle_time, counter_of_simulations;     // zmienne pomocnicze potrzebne do obliczania fDt
long start_time = clock();          // czas od poczatku dzialania aplikacji  
long group_existing_time = clock();    // czas od początku istnienia grupy roboczej (czas od uruchom. pierwszej aplikacji)      

multicast_net *multi_reciv;         // wsk do obiektu zajmujacego sie odbiorem komunikatow
multicast_net *multi_send;          //   -||-  wysylaniem komunikatow

HANDLE threadReciv;                 // uchwyt wątku odbioru komunikatów
extern HWND main_window;
CRITICAL_SECTION m_cs;               // do synchronizacji wątków

bool SHIFT_pressed = 0;
bool CTRL_pressed = 0;
bool ALT_pressed = 0;
bool L_pressed = 0;
//bool rejestracja_uczestnikow = true;   // rejestracja trwa do momentu wzięcia przedmiotu przez któregokolwiek uczestnika,
// w przeciwnym razie trzeba by przesyłać cały state środowiska nowicjuszowi

// Parametry widoku:
extern ViewParameters par_view;

bool mouse_control = 0;                   // sterowanie pojazdem za pomocą myszki
int cursor_x, cursor_y;                         // polożenie kursora myszki w chwili włączenia sterowania

bool autopilot_presentation_mode = 0;                // czy pokazywać test autopilota
float autopilot_presentation_current_time = 0;       // czas jaki upłynął od początku testu autopilota
float autopilot_time_step = 0.01;           // stały krok czasowy wolny od możliwości sprzętowych (zamiast fDt)
float autopilot_test_time = 600;              // całkowity czas testu

extern float TransferSending(int ID_receiver, int transfer_type, float transfer_value);

struct Frame
{
	int iID;
	int frame_type;
	ObjectState state;
	
	int iID_receiver;      // nr ID adresata wiadomości (pozostali uczestnicy powinni wiadomość zignorować)

	int item_number;     // nr przedmiotu, który został wzięty lub odzyskany
	Vector3 vdV_collision;     // wektor prędkości wyjściowej po kolizji (uczestnik o wskazanym adresie powinien 
	// przyjąć tą prędkość)  

	int transfer_type;        // gotówka, paliwo
	float transfer_value;  // ilość gotówki lub paliwa 
	int team_number;

	long existing_time;        // czas jaki uplynął od uruchomienia programu
};


//******************************************
// Funkcja obsługi wątku odbioru komunikatów 
DWORD WINAPI ReceiveThreadFunction(void *ptr)
{
	multicast_net *pmt_net = (multicast_net*)ptr;  // wskaŸnik do obiektu klasy multicast_net
	int size;                                 // liczba bajtów ramki otrzymanej z sieci
	Frame frame;
	ObjectState state;
	
	while (1)
	{
		size = pmt_net->reciv((char*)&frame, sizeof(Frame));   // oczekiwanie na nadejście ramki 
		// Lock the Critical section
		EnterCriticalSection(&m_cs);               // wejście na ścieżkę krytyczną - by inne wątki (np. główny) nie współdzielił 

		switch (frame.frame_type)
		{
		case OBJECT_STATE:           // podstawowy typ ramki informującej o stanie obiektu              
		{
			state = frame.state;
			//fprintf(f,"odebrano state iID = %d, ID dla mojego obiektu = %d\n",state.iID,my_vehicle->iID);
			if ((frame.iID != my_vehicle->iID))          // jeśli to nie mój własny Object
			{

				if ((network_vehicles.size() == 0) || (network_vehicles[frame.iID] == NULL))         // nie ma jeszcze takiego obiektu w tablicy -> trzeba go stworzyć
				{
					MovableObject *ob = new MovableObject(&terrain);
					ob->iID = frame.iID;
					network_vehicles[frame.iID] = ob;
					if (frame.existing_time > group_existing_time) group_existing_time = frame.existing_time;
					ob->ChangeState(state);   // aktualizacja stanu obiektu obcego 
					terrain.InsertObjectIntoSectors(ob);
					// wysłanie nowemu uczestnikowi informacji o wszystkich wziętych przedmiotach:
					for (long i = 0; i < terrain.number_of_items; i++)
						if ((terrain.p[i].to_take == 0) && (terrain.p[i].if_taken_by_me))
						{
							Frame frame;
							frame.frame_type = ITEM_TAKING;
							frame.item_number = i;
							frame.state = my_vehicle->State();
							frame.iID = my_vehicle->iID;
							int iSIZE = multi_send->send((char*)&frame, sizeof(Frame));
						}

				}
				else if ((network_vehicles.size() > 0) && (network_vehicles[frame.iID] != NULL))
				{
					terrain.DeleteObjectsFromSectors(network_vehicles[frame.iID]);
					network_vehicles[frame.iID]->ChangeState(state);   // aktualizacja stanu obiektu obcego 	
					terrain.InsertObjectIntoSectors(network_vehicles[frame.iID]);
				}
				
			}
			break;
		}
		case ITEM_TAKING:            // frame informująca, że ktoś wziął przedmiot o podanym numerze
		{
			state = frame.state;
			if ((frame.item_number < terrain.number_of_items) && (frame.iID != my_vehicle->iID))
			{
				terrain.p[frame.item_number].to_take = 0;
				terrain.p[frame.item_number].if_taken_by_me = 0;
			}
			break;
		}
		case ITEM_RENEWAL:       // frame informujaca, że przedmiot wcześniej wzięty pojawił się znowu w tym samym miejscu
		{
			if (frame.item_number < terrain.number_of_items)
				terrain.p[frame.item_number].to_take = 1;
			break;
		}
		case COLLISION:                       // frame informująca o tym, że Object uległ kolizji
		{
			if (frame.iID_receiver == my_vehicle->iID)  // ID pojazdu, który uczestniczył w kolizji zgadza się z moim ID 
			{
				my_vehicle->vdV_collision = frame.vdV_collision; // przepisuje poprawkę własnej prędkości
				my_vehicle->iID_collider = my_vehicle->iID; // ustawiam nr. kolidujacego jako własny na znak, że powinienem poprawić prędkość
			}
			break;
		}
		case TRANSFER:                       // frame informująca o przelewie pieniężnym lub przekazaniu towaru    
		{
			if (frame.iID_receiver == my_vehicle->iID)  // ID pojazdu, ktory otrzymal przelew zgadza się z moim ID 
			{
				if (frame.transfer_type == MONEY)
					my_vehicle->state.money += frame.transfer_value;
				else if (frame.transfer_type == FUEL)
					my_vehicle->state.amount_of_fuel += frame.transfer_value;

				// należałoby jeszcze przelew potwierdzić (w UDP ramki mogą być gubione!)
			}
			break;
		}

		// ROZSZERZENIE
		// jak agent zobaczy ofertę od kogoś, podejmuje decyzję co zrobić - przyjąć czy odrzucić ofertę
		case BUY_OFFER:
		{
			if (if_autonomous_control && frame.iID != my_vehicle->iID)  // nie będę samemu sobie sprzedawał
			{
				if (AutoPilot::Decide(false, my_vehicle->state.amount_of_fuel)) {
					Frame reply_frame;
					reply_frame.frame_type = BUY_ACCEPT;
					reply_frame.transfer_value = frame.transfer_value;
					reply_frame.iID = my_vehicle->iID;
					reply_frame.iID_receiver = frame.iID;
					int iRozmiar = multi_send->send((char*)& reply_frame, sizeof(Frame));
					sprintf(par_view.inscription1, "Nasz_agent_zgadza_sie_sprzedac_%d_jednostek_paliwa_za_%d_monet_", BASE_VOLUME, int(frame.transfer_value));
				}
			}
			break;
		}
		
		// jak wyżej, ale dla ofert sprzedaży
		case SELL_OFFER:
		{
			if (if_autonomous_control && frame.iID != my_vehicle->iID)  // nie będę samemu sobie sprzedawał
			{
				if (AutoPilot::Decide(true, my_vehicle->state.amount_of_fuel)) {
					Frame reply_frame;
					reply_frame.frame_type = SELL_ACCEPT;
					reply_frame.transfer_value = frame.transfer_value;
					reply_frame.iID = my_vehicle->iID;
					reply_frame.iID_receiver = frame.iID;
					int iRozmiar = multi_send->send((char*)& reply_frame, sizeof(Frame));
					sprintf(par_view.inscription1, "Nasz_agent_zgadza_sie_kupic_%d_jednostek_paliwa_za_%d_monet_", BASE_VOLUME, int(frame.transfer_value));
				}
			}
			break;
		}

		// jak autor oferty widzi od kogoś zainteresowanie, to 
		// sprawdza czy oferta nadal aktualna i jak tak, to wysyła swoją część transakcji
		case BUY_ACCEPT:
		{
			if (frame.iID_receiver == my_vehicle->iID && my_vehicle->start_buying &&
				my_vehicle->state.money >= frame.transfer_value)
			{
				Frame reply_frame;
				reply_frame.frame_type = BUY_PART;
				my_vehicle->state.money -= frame.transfer_value;
				reply_frame.transfer_value = frame.transfer_value;
				reply_frame.iID = my_vehicle->iID;
				reply_frame.iID_receiver = frame.iID;
				int iRozmiar = multi_send->send((char*)& reply_frame, sizeof(Frame));
				sprintf(par_view.inscription1, "Nasz_agent_wyslal_%d_monet_i_czeka_na_%d_jednostek_paliwa_", int(frame.transfer_value), BASE_VOLUME);
			}
			break;
		}

		// jak wyżej, dla oferty sprzedaży
		case SELL_ACCEPT:
		{
			if (frame.iID_receiver == my_vehicle->iID && my_vehicle->start_selling &&
				my_vehicle->state.amount_of_fuel >= BASE_VOLUME)
			{
				Frame reply_frame;
				reply_frame.frame_type = SELL_PART;
				my_vehicle->state.amount_of_fuel -= BASE_VOLUME;
				reply_frame.transfer_value = frame.transfer_value;
				reply_frame.iID = my_vehicle->iID;
				reply_frame.iID_receiver = frame.iID;
				int iRozmiar = multi_send->send((char*)& reply_frame, sizeof(Frame));
				sprintf(par_view.inscription1, "Nasz_agent_wyslal_%d_jednostek_paliwa_i_czeka_na_%d_monet_", BASE_VOLUME, int(frame.transfer_value));
			}
			break;
		}

		// osoba która zgodziła się na handel otrzymuje swoją część i wysyła w ramce TRANSFER należność do autora oferty
		case BUY_PART:
		{
			if (frame.iID_receiver == my_vehicle->iID) {
				my_vehicle->state.money += frame.transfer_value;
				Frame reply_frame;
				reply_frame.frame_type = TRANSFER;
				reply_frame.transfer_type = FUEL;
				my_vehicle->state.amount_of_fuel -= BASE_VOLUME;
				reply_frame.transfer_value = BASE_VOLUME;
				reply_frame.iID = my_vehicle->iID;
				reply_frame.iID_receiver = frame.iID;
				int iRozmiar = multi_send->send((char*)& reply_frame, sizeof(Frame));
				sprintf(par_view.inscription1, "Nasz_agent_otrzymal_%d_monet_i_wyslal_%d_jednostek_paliwa_", int(frame.transfer_value), BASE_VOLUME);
			}
			break;
		}

		// jak wyżej, dla oferty sprzedaży
		case SELL_PART:
		{
			if (frame.iID_receiver == my_vehicle->iID) {
				my_vehicle->state.amount_of_fuel += BASE_VOLUME;
				Frame reply_frame;
				reply_frame.frame_type = TRANSFER;
				reply_frame.transfer_type = MONEY;
				my_vehicle->state.money -= frame.transfer_value;
				reply_frame.transfer_value = frame.transfer_value;
				reply_frame.iID = my_vehicle->iID;
				reply_frame.iID_receiver = frame.iID;
				int iRozmiar = multi_send->send((char*)& reply_frame, sizeof(Frame));
				sprintf(par_view.inscription1, "Nasz_agent_otrzymal_%d_jednostek_paliwa_i_wyslal_%d_monet_", int(frame.transfer_value), BASE_VOLUME);
			}
			break;
		}
		



		} // switch po typach ramek
		// Opuszczenie ścieżki krytycznej / Release the Critical section
		LeaveCriticalSection(&m_cs);               // wyjście ze ścieżki krytycznej
	}  // while(1)
	return 1;
}

// *****************************************************************
// ****    Wszystko co trzeba zrobić podczas uruchamiania aplikacji
// ****    poza grafiką   
void InteractionInitialisation()
{
	DWORD dwThreadId;

	my_vehicle = new MovableObject(&terrain);    // tworzenie wlasnego obiektu
	if (if_different_skills == false)
		my_vehicle->planting_skills = my_vehicle->money_collection_skills = my_vehicle->fuel_collection_skills = 1.0;

	ap = new AutoPilot();

	VW_cycle_time = clock();             // pomiar aktualnego czasu

	// obiekty sieciowe typu multicast (z podaniem adresu WZR oraz numeru portu)
	multi_reciv = new multicast_net("224.10.12.100", 10001);      // Object do odbioru ramek sieciowych
	multi_send = new multicast_net("224.10.12.100", 10001);       // Object do wysyłania ramek

	// uruchomienie watku obslugujacego odbior komunikatow
	threadReciv = CreateThread(
		NULL,                        // no security attributes
		0,                           // use default stack size
		ReceiveThreadFunction,       // thread function
		(void *)multi_reciv,         // argument to thread function
		0,                           // use default creation flags
		&dwThreadId);                // returns the thread identifier
		
}


// *****************************************************************
// ****    Wszystko co trzeba zrobić w każdym cyklu działania 
// ****    aplikacji poza grafiką 
void VirtualWorldCycle()
{
	counter_of_simulations++;

	// obliczenie średniego czasu pomiędzy dwoma kolejnnymi symulacjami po to, by zachować  fizycznych 
	if (counter_of_simulations % 50 == 0)          // jeśli licznik cykli przekroczył pewną wartość, to
	{                                   // należy na nowo obliczyć średni czas cyklu fDt
		char text[200];
		long prev_time = VW_cycle_time;
		VW_cycle_time = clock();
		float fFps = (50 * CLOCKS_PER_SEC) / (float)(VW_cycle_time - prev_time);
		if (fFps != 0) fDt = 1.0 / fFps; else fDt = 1;

		sprintf(par_view.inscription1, " %0.0f_fps, fuel = %0.2f, money = %d,", fFps, my_vehicle->state.amount_of_fuel, my_vehicle->state.money);
		if (counter_of_simulations % 500 == 0) sprintf(par_view.inscription2, "");
	}

	if (autopilot_presentation_mode)
	{
		ap->AutoControl(my_vehicle);
		terrain.DeleteObjectsFromSectors(my_vehicle);
		my_vehicle->Simulation(autopilot_time_step);
		terrain.InsertObjectIntoSectors(my_vehicle);
		autopilot_presentation_current_time += autopilot_time_step;
		sprintf(par_view.inscription2, "POKAZ TESTU AUTOPILOTA: CZAS = %f", autopilot_presentation_current_time);
		if (autopilot_presentation_current_time >= autopilot_test_time)
		{
			autopilot_presentation_mode = false;
			MessageBox(main_window, "Koniec pokazu testu autopilota", "Czy chcesz zamknac program?", MB_OK);
			SendMessage(main_window, WM_DESTROY, 0, 0);
		}

	}
	else
	{
		terrain.DeleteObjectsFromSectors(my_vehicle);
		my_vehicle->Simulation(fDt);                    // symulacja własnego obiektu
		terrain.InsertObjectIntoSectors(my_vehicle);
	}


	if ((my_vehicle->iID_collider > -1) &&             // wykryto kolizję - wysyłam specjalną ramkę, by poinformować o tym drugiego uczestnika
		(my_vehicle->iID_collider != my_vehicle->iID)) // oczywiście wtedy, gdy nie chodzi o mój pojazd
	{
		Frame frame;
		frame.frame_type = COLLISION;
		frame.iID_receiver = my_vehicle->iID_collider;
		frame.vdV_collision = my_vehicle->vdV_collision;
		frame.iID = my_vehicle->iID;
		int iRozmiar = multi_send->send((char*)&frame, sizeof(Frame));

		char text[128];
		sprintf(par_view.inscription2, "Kolizja_z_obiektem_o_ID = %d", my_vehicle->iID_collider);
		//SetWindowText(main_window,text);

		my_vehicle->iID_collider = -1;
	}

	// wyslanie komunikatu o stanie obiektu przypisanego do aplikacji (my_vehicle):    

	Frame frame;
	frame.frame_type = OBJECT_STATE;
	frame.state = my_vehicle->State();         // state własnego obiektu 
	frame.iID = my_vehicle->iID;
	frame.existing_time = clock() - start_time;
	int iRozmiar = multi_send->send((char*)&frame, sizeof(Frame));



	// wzięcie przedmiotu -> wysyłanie ramki 
	if (my_vehicle->number_of_taking_item > -1)
	{
		Frame frame;
		frame.frame_type = ITEM_TAKING;
		frame.item_number = my_vehicle->number_of_taking_item;
		frame.state = my_vehicle->State();
		frame.iID = my_vehicle->iID;
		int iRozmiar = multi_send->send((char*)&frame, sizeof(Frame));

		sprintf(par_view.inscription2, "Wziecie_przedmiotu_o_wartosci_ %f", my_vehicle->taking_value);

		my_vehicle->number_of_taking_item = -1;
		my_vehicle->taking_value = 0;
	}

	// odnawianie się przedmiotu -> wysyłanie ramki
	if (my_vehicle->number_of_renewed_item > -1)
	{                             // jeśli minął pewnien okres czasu przedmiot może zostać przywrócony
		Frame frame;
		frame.frame_type = ITEM_RENEWAL;
		frame.item_number = my_vehicle->number_of_renewed_item;
		frame.iID = my_vehicle->iID;
		int iRozmiar = multi_send->send((char*)&frame, sizeof(Frame));


		my_vehicle->number_of_renewed_item = -1;
	}

	// ROZSZERZENIE
	// czy staram się kupić paliwo i minęło przynajmniej 5 sekund od ostatniej oferty - by nie spamować wiecznie ofert
	if (if_autonomous_control && my_vehicle->start_buying == true &&
		duration_cast<milliseconds>(system_clock::now().time_since_epoch() - my_vehicle->last_buy_offer) > milliseconds(OFFER_TIME))
	{
		Frame frame;
		frame.frame_type = BUY_OFFER;
		frame.transfer_value = AutoPilot::OfferPrice(true, my_vehicle);
		frame.iID = my_vehicle->iID;
		my_vehicle->last_buy_offer = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
		sprintf(par_view.inscription1, "Wyslanie_oferty_kupna_%d_jednostek_paliwa_za_%d_monet_", BASE_VOLUME, int(frame.transfer_value));
		int iRozmiar = multi_send->send((char*)& frame, sizeof(Frame));
	}
	//analogicznie dla sprzedaży
	else if (if_autonomous_control && my_vehicle->start_selling == true &&
		duration_cast<milliseconds>(system_clock::now().time_since_epoch() - my_vehicle->last_sell_offer) > milliseconds(OFFER_TIME))
	{
		Frame frame;
		frame.frame_type = SELL_OFFER;
		frame.transfer_value = AutoPilot::OfferPrice(false, my_vehicle);
		frame.iID = my_vehicle->iID;
		my_vehicle->last_sell_offer = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
		sprintf(par_view.inscription1, "Wyslanie_oferty_sprzedazy_%d_jednostek_paliwa_za_%d_monet_", BASE_VOLUME, frame.transfer_value);
		int iRozmiar = multi_send->send((char*)& frame, sizeof(Frame));
	}




	// --------------------------------------------------------------------
	// --------------- MIEJSCE NA ALGORYTM STEROWANIA ---------------------
	// (dobór siły F w granicach (-F_max/2, F_max), kąta skrętu kół wheel_turn_angle (-alpha_max, alpha_max) oraz
	// siły o hamowania breaking_degree (0,1) [+ decyzji w związku ze współpracą] w zależności od sytuacji)
	if (if_autonomous_control)
	{
		ap->AutoControl(my_vehicle);
		sprintf(par_view.inscription2, "F=%f,_ham=%f,_alfa=%f", my_vehicle->F, my_vehicle->breaking_degree, my_vehicle->state.wheel_turn_angle);
	}


}

// *****************************************************************
// ****    Wszystko co trzeba zrobić podczas zamykania aplikacji
// ****    poza grafiką 
void EndOfInteraction()
{
	fprintf(f, "Koniec interakcji\n");
	fclose(f);
}

// Funkcja wysylajaca ramke z przekazem, zwraca zrealizowaną wartość przekazu
float TransferSending(int ID_receiver, int transfer_type, float transfer_value)
{
	Frame frame;
	frame.frame_type = TRANSFER;
	frame.iID_receiver = ID_receiver;
	frame.transfer_type = transfer_type;
	frame.transfer_value = transfer_value;
	frame.iID = my_vehicle->iID;

	// tutaj należałoby uzyskać potwierdzenie przekazu zanim sumy zostaną odjęte
	if (transfer_type == MONEY)
	{
		if (my_vehicle->state.money < transfer_value)
			frame.transfer_value = my_vehicle->state.money;
		my_vehicle->state.money -= frame.transfer_value;
		sprintf(par_view.inscription2, "Przelew_sumy_ %f _na_rzecz_ID_ %d", transfer_value, ID_receiver);
	}
	else if (transfer_type == FUEL)
	{
		if (my_vehicle->state.amount_of_fuel < transfer_value)
			frame.transfer_value = my_vehicle->state.amount_of_fuel;
		my_vehicle->state.amount_of_fuel -= frame.transfer_value;
		sprintf(par_view.inscription2, "Przekazanie_paliwa_w_ilosci_ %f _na_rzecz_ID_ %d", transfer_value, ID_receiver);
	}

	if (frame.transfer_value > 0)
		int iRozmiar = multi_send->send((char*)&frame, sizeof(Frame));

	return frame.transfer_value;
}





//deklaracja funkcji obslugi okna
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);


HWND main_window;                   // uchwyt do okna aplikacji
HDC g_context = NULL;        // uchwyt kontekstu graficznego

bool terrain_edition_mode = 0;

//funkcja Main - dla Windows
int WINAPI WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nCmdShow)
{
	//Initilize the critical section:
	InitializeCriticalSection(&m_cs);

	MSG system_message;		  //innymi slowy "komunikat"
	WNDCLASS window_class; //klasa głównego okna aplikacji

	static char class_name[] = "Basic";

	//Definiujemy klase głównego okna aplikacji
	//Okreslamy tu wlasciwosci okna, szczegoly wygladu oraz
	//adres funkcji przetwarzajacej komunikaty
	window_class.style = CS_HREDRAW | CS_VREDRAW;
	window_class.lpfnWndProc = WndProc; //adres funkcji realizującej przetwarzanie meldunków 
	window_class.cbClsExtra = 0;
	window_class.cbWndExtra = 0;
	window_class.hInstance = hInstance; //identyfikator procesu przekazany przez MS Windows podczas uruchamiania programu
	window_class.hIcon = 0;
	window_class.hCursor = LoadCursor(0, IDC_ARROW);
	window_class.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
	window_class.lpszMenuName = "Menu";
	window_class.lpszClassName = class_name;

	//teraz rejestrujemy klasę okna głównego
	RegisterClass(&window_class);

	/*tworzymy main_window główne
	main_window będzie miało zmienne rozmiary, listwę z tytułem, menu systemowym
	i przyciskami do zwijania do ikony i rozwijania na cały ekran, po utworzeniu
	będzie widoczne na ekranie */
	main_window = CreateWindow(class_name, "WZR 2018/19, temat 4, wersja d [Y-autopilot, F10-test autop.]", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		100, 100, 1200, 800, NULL, NULL, hInstance, NULL);


	ShowWindow(main_window, nCmdShow);

	//odswiezamy zawartosc okna
	UpdateWindow(main_window);



	// GŁÓWNA PĘTLA PROGRAMU

	// pobranie komunikatu z kolejki jeśli funkcja PeekMessage zwraca wartość inną niż FALSE,
	// w przeciwnym wypadku symulacja wirtualnego świata wraz z wizualizacją
	ZeroMemory(&system_message, sizeof(system_message));
	while (system_message.message != WM_QUIT)
	{
		if (PeekMessage(&system_message, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&system_message);
			DispatchMessage(&system_message);
		}
		else
		{
			VirtualWorldCycle();    // Cykl wirtualnego świata
			InvalidateRect(main_window, NULL, FALSE);
		}
	}

	return (int)system_message.wParam;
}

// ************************************************************************
// ****    Obsługa klawiszy służących do sterowania obiektami lub
// ****    widokami 
void MessagesHandling(UINT message_type, WPARAM wParam, LPARAM lParam)
{

	int LCONTROL = GetKeyState(VK_LCONTROL);
	int RCONTROL = GetKeyState(VK_RCONTROL);
	int LALT = GetKeyState(VK_LMENU);
	int RALT = GetKeyState(VK_RMENU);


	switch (message_type)
	{

	case WM_LBUTTONDOWN: //reakcja na lewy przycisk myszki
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		if (mouse_control)
			my_vehicle->F = my_vehicle->F_max;        // siła pchająca do przodu

		break;
	}
	case WM_RBUTTONDOWN: //reakcja na prawy przycisk myszki
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		int LSHIFT = GetKeyState(VK_LSHIFT);   // sprawdzenie czy lewy Shift wciśnięty, jeśli tak, to LSHIFT == 1
		int RSHIFT = GetKeyState(VK_RSHIFT);

		if (mouse_control)
			my_vehicle->F = -my_vehicle->F_max / 2;        // siła pchająca do tylu
		else if (wParam & MK_SHIFT)                    // odznaczanie wszystkich obiektów   
		{
			for (long i = 0; i < terrain.number_of_selected_items; i++)
				terrain.p[terrain.selected_items[i]].if_selected = 0;
			terrain.number_of_selected_items = 0;
		}
		else                                          // zaznaczenie obiektów
		{
			RECT r;
			//GetWindowRect(main_window,&r);
			GetClientRect(main_window, &r);
			//Vector3 w = Cursor3dCoordinates(x, r.bottom - r.top - y);
			Vector3 w = terrain.Cursor3D_CoordinatesWithoutParallax(x, r.bottom - r.top - y);


			//float radius = (w - point_click).length();
			float min_dist = 1e10;
			long index_min = -1;
			bool if_movable_obj;
			for (map<int, MovableObject*>::iterator it = network_vehicles.begin(); it != network_vehicles.end(); ++it)
			{
				if (it->second)
				{
					MovableObject *ob = it->second;
					float xx, yy, zz;
					ScreenCoordinates(&xx, &yy, &zz, ob->state.vPos);
					yy = r.bottom - r.top - yy;
					float odl_kw = (xx - x)*(xx - x) + (yy - y)*(yy - y);
					if (min_dist > odl_kw)
					{
						min_dist = odl_kw;
						index_min = ob->iID;
						if_movable_obj = 1;
					}
				}
			}
		

			// trzeba to przerobić na wersję sektorową, gdyż przedmiotów może być dużo!
			// niestety nie jest to proste. 

			//Item **item_tab_pointer = NULL;
			//long number_of_items_in_radius = terrain.ItemsInRadius(&item_tab_pointer, w,100);

			for (long i = 0; i < terrain.number_of_items; i++)
			{
				float xx, yy, zz;
				Vector3 placement;
				if ((terrain.p[i].type == ITEM_EDGE) || (terrain.p[i].type == ITEM_WALL))
				{
					placement = (terrain.p[terrain.p[i].param_i[0]].vPos + terrain.p[terrain.p[i].param_i[1]].vPos) / 2;
				}
				else
					placement = terrain.p[i].vPos;
				ScreenCoordinates(&xx, &yy, &zz, placement);
				yy = r.bottom - r.top - yy;
				float odl_kw = (xx - x)*(xx - x) + (yy - y)*(yy - y);
				if (min_dist > odl_kw)
				{
					min_dist = odl_kw;
					index_min = i;
					if_movable_obj = 0;
				}
			}

			if (index_min > -1)
			{
				//fprintf(f,"zaznaczono przedmiot %d pol = (%f, %f, %f)\n",ind_min,terrain.p[ind_min].vPos.x,terrain.p[ind_min].vPos.y,terrain.p[ind_min].vPos.z);
				//terrain.p[ind_min].if_selected = 1 - terrain.p[ind_min].if_selected;
				if (if_movable_obj)
				{
					network_vehicles[index_min]->if_selected = 1 - network_vehicles[index_min]->if_selected;

					if (network_vehicles[index_min]->if_selected)
						sprintf(par_view.inscription2, "zaznaczono_ obiekt_ID_%d", network_vehicles[index_min]->iID);
				}
				else
				{
					terrain.SelectUnselectItemOrGroup(index_min);
				}
				//char lan[256];
				//sprintf(lan, "kliknięto w przedmiot %d pol = (%f, %f, %f)\n",ind_min,terrain.p[ind_min].vPos.x,terrain.p[ind_min].vPos.y,terrain.p[ind_min].vPos.z);
				//SetWindowText(main_window,lan);
			}
			Vector3 point_click = Cursor3dCoordinates(x, r.bottom - r.top - y);

		}

		break;
	}
	case WM_MBUTTONDOWN: //reakcja na środkowy przycisk myszki : uaktywnienie/dezaktywacja sterwania myszkowego
	{
		mouse_control = 1 - mouse_control;
		cursor_x = LOWORD(lParam);
		cursor_y = HIWORD(lParam);
		break;
	}
	case WM_LBUTTONUP: //reakcja na puszczenie lewego przycisku myszki
	{
		if (mouse_control)
			my_vehicle->F = 0.0;        // siła pchająca do przodu
		break;
	}
	case WM_RBUTTONUP: //reakcja na puszczenie lewy przycisk myszki
	{
		if (mouse_control)
			my_vehicle->F = 0.0;        // siła pchająca do przodu
		break;
	}
	case WM_MOUSEMOVE:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		if (mouse_control)
		{
			float wheel_turn_angle = (float)(cursor_x - x) / 20;
			if (wheel_turn_angle > 45) wheel_turn_angle = 45;
			if (wheel_turn_angle < -45) wheel_turn_angle = -45;
			my_vehicle->state.wheel_turn_angle = PI*wheel_turn_angle / 180;
		}
		break;
	}
	case WM_MOUSEWHEEL:     // ruch kółkiem myszy -> przybliżanie, oddalanie widoku
	{
		int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);  // dodatni do przodu, ujemny do tyłu
		//fprintf(f,"zDelta = %d\n",zDelta);          // zwykle +-120, jak się bardzo szybko zakręci to czasmi wyjdzie +-240
		if (zDelta > 0){
			if (par_view.distance > 0.5) par_view.distance /= 1.2;
			else par_view.distance = 0;
		}
		else {
			if (par_view.distance > 0) par_view.distance *= 1.2;
			else par_view.distance = 0.5;
		}

		break;
	}
	case WM_KEYDOWN:
	{

		switch (LOWORD(wParam))
		{
		case VK_SHIFT:
		{
			SHIFT_pressed = 1;
			break;
		}
		case VK_CONTROL:
		{
			CTRL_pressed = 1;
			break;
		}
		case VK_MENU:
		{
			ALT_pressed = 1;
			break;
		}

		case VK_SPACE:
		{
			my_vehicle->breaking_degree = 1.0;       // stopieñ hamowania (reszta zależy od siły docisku i wsp. tarcia)
			break;                       // 1.0 to maksymalny stopieñ (np. zablokowanie kół)
		}
		case VK_UP:
		{
			if (CTRL_pressed && par_view.top_view)
				par_view.shift_to_bottom += par_view.distance / 2;       // przesunięcie widoku z kamery w górę
			else
				my_vehicle->F = my_vehicle->F_max;        // siła pchająca do przodu
			break;
		}
		case VK_DOWN:
		{
			if (CTRL_pressed && par_view.top_view)
				par_view.shift_to_bottom -= par_view.distance / 2;       // przesunięcie widoku z kamery w dół 
			else
				my_vehicle->F = -my_vehicle->F_max / 2;        // sila pchajaca do tylu
			break;
		}
		case VK_LEFT:
		{
			if (CTRL_pressed && par_view.top_view)
				par_view.shift_to_right += par_view.distance / 2;
			else
			{
				if (my_vehicle->wheel_turn_speed < 0) {
					my_vehicle->wheel_turn_speed = 0;
					my_vehicle->if_keep_steer_wheel = true;
				}
				else {
					if (SHIFT_pressed) my_vehicle->wheel_turn_speed = 0.5;
					else my_vehicle->wheel_turn_speed = 0.5 / 4;
				}
			}

			break;
		}
		case VK_RIGHT:
		{
			if (CTRL_pressed && par_view.top_view)
				par_view.shift_to_right -= par_view.distance / 2;
			else
			{
				if (my_vehicle->wheel_turn_speed > 0) {
					my_vehicle->wheel_turn_speed = 0;
					my_vehicle->if_keep_steer_wheel = true;
				}
				else {
					if (SHIFT_pressed) my_vehicle->wheel_turn_speed = -0.5;
					else my_vehicle->wheel_turn_speed = -0.5 / 4;
				}
			}
			break;
		}
		case VK_HOME:
		{
			if (CTRL_pressed && par_view.top_view)
				par_view.shift_to_right = par_view.shift_to_bottom = 0;

			break;
		}
		case 'W':   // przybliżenie widoku
		{
			//initial_camera_position = initial_camera_position - initial_camera_direction*0.3;
			if (par_view.distance > 0.5)par_view.distance /= 1.2;
			else par_view.distance = 0;
			break;
		}
		case 'S':   // distance widoku
		{
			//initial_camera_position = initial_camera_position + initial_camera_direction*0.3; 
			if (par_view.distance > 0) par_view.distance *= 1.2;
			else par_view.distance = 0.5;
			break;
		}
		case 'Q':   // widok z góry
		{
			par_view.top_view = 1 - par_view.top_view;
			if (par_view.top_view)
				SetWindowText(main_window, "Włączono widok z góry!");
			else
				SetWindowText(main_window, "Wyłączono widok z góry.");
			break;
		}
		case 'E':   // obrót kamery ku górze (względem lokalnej osi z)
		{
			par_view.cam_angle_z += PI * 5 / 180;
			break;
		}
		case 'D':   // obrót kamery ku dołowi (względem lokalnej osi z)
		{
			par_view.cam_angle_z -= PI * 5 / 180;
			break;
		}
		case 'A':   // włączanie, wyłączanie trybu śledzenia obiektu
		{
			par_view.tracking = 1 - par_view.tracking;
			break;
		}
		case 'Z':   // zoom - zmniejszenie kąta widzenia
		{
			par_view.zoom /= 1.1;
			RECT rc;
			GetClientRect(main_window, &rc);
			WindowSizeChange(rc.right - rc.left, rc.bottom - rc.top);
			break;
		}
		case 'X':   // zoom - zwiększenie kąta widzenia
		{
			par_view.zoom *= 1.1;
			RECT rc;
			GetClientRect(main_window, &rc);
			WindowSizeChange(rc.right - rc.left, rc.bottom - rc.top);
			break;
		}

		case 'F':  // przekazanie 10 kg paliwa pojazdom zaznaczonym
		{
			for (map<int, MovableObject*>::iterator it = network_vehicles.begin(); it != network_vehicles.end(); ++it)
			{
				if (it->second)
				{
					MovableObject *ob = it->second;
					if (ob->if_selected)
						float ilosc_p = TransferSending(ob->iID, FUEL, 10);
				}
			}
			break;
		}
		case 'G':  // przekazanie 100 jednostek gotowki pojazdom zaznaczonym
		{
			for (map<int, MovableObject*>::iterator it = network_vehicles.begin(); it != network_vehicles.end(); ++it)
			{
				if (it->second)
				{
					MovableObject *ob = it->second;
					if (ob->if_selected)
						float ilosc_p = TransferSending(ob->iID, MONEY, 100);
				}
			}
			break;
		}
		case 'Y':
			if (if_autonomous_control) if_autonomous_control = false;
			else if_autonomous_control = true;

			if (if_autonomous_control)
				sprintf(par_view.inscription1, "Wlaczenie_autosterowania,_teraz_pojazd_staje_sie_samochodem");
			else
			{
				my_vehicle->state.wheel_turn_angle = my_vehicle->F = my_vehicle->breaking_degree = 0;
				sprintf(par_view.inscription1, "Wylaczenie_autosterowania");
			}
			break;
		case 'L':     // rozpoczęcie zaznaczania metodą lasso
			L_pressed = true;
			break;
		case VK_F10:  // sumulacja automatycznego sterowania obiektem w celu jego oceny poza czasem rzeczywistym 
		{
			//float czas_proby = 600;               // czas testu w sekundach
			bool if_parameters_optimal = true;    // pełne umiejętności, stały czas kroku
			Terrain t2;
			MovableObject *Object = new MovableObject(&t2);
			//AutoPilot *a = new AutoPilot(&t2,Object);
			float time_step = -1;
			if (if_parameters_optimal)
			{
				Object->planting_skills = Object->money_collection_skills = Object->fuel_collection_skills = 1.0;
				time_step = autopilot_time_step;
			}
			else
			{
				Object->planting_skills = my_vehicle->planting_skills;
				Object->money_collection_skills = my_vehicle->money_collection_skills;
				Object->fuel_collection_skills = my_vehicle->fuel_collection_skills;
				time_step = fDt;
			}
			long money_at_start = Object->state.money;

			char lanc[256];
			sprintf(lanc, "Test sterowania autonomicznego dla um.got = %1.1f, um.pal = %1.1f, krok = %f[s], - prosze czekac!", Object->money_collection_skills, Object->fuel_collection_skills, time_step);
			SetWindowText(main_window, lanc);
			long t_start = clock();


			ap->ControlTest(Object, time_step, autopilot_test_time);


			char lan[512], lan1[512];
			sprintf(lan, "Uzyskano %d gotowki w ciagu %f sekund (krok = %f), ilosc paliwa = %f, czy pokazac caly test?", Object->state.money - money_at_start, autopilot_test_time, time_step, Object->state.amount_of_fuel);
			sprintf(lan1, "Test autonomicznego sterowania, czas testu = %f s.", (float)(clock() - t_start) / CLOCKS_PER_SEC);
			int result = MessageBox(main_window, lan, lan1, MB_YESNO);
			if (result == 6)
			{
				autopilot_presentation_mode = true;
				autopilot_presentation_current_time = 0;
				my_vehicle->planting_skills = my_vehicle->money_collection_skills = my_vehicle->fuel_collection_skills = 1.0;
			}


			break;
		}


		} // switch po klawiszach

		break;
	}

	case WM_KEYUP:
	{
		switch (LOWORD(wParam))
		{
		case VK_SHIFT:
		{
			SHIFT_pressed = 0;
			break;
		}
		case VK_CONTROL:
		{
			CTRL_pressed = 0;
			break;
		}
		case VK_MENU:
		{
			ALT_pressed = 0;
			break;
		}
		case 'L':     // zakonczenie zaznaczania metodą lasso
			L_pressed = false;
			break;
		case VK_SPACE:
		{
			my_vehicle->breaking_degree = 0.0;
			break;
		}
		case VK_UP:
		{
			my_vehicle->F = 0.0;

			break;
		}
		case VK_DOWN:
		{
			my_vehicle->F = 0.0;
			break;
		}
		case VK_LEFT:
		{
			if (my_vehicle->if_keep_steer_wheel) my_vehicle->wheel_turn_speed = -0.5 / 4;
			else my_vehicle->wheel_turn_speed = 0;
			my_vehicle->if_keep_steer_wheel = false;
			break;
		}
		case VK_RIGHT:
		{
			if (my_vehicle->if_keep_steer_wheel) my_vehicle->wheel_turn_speed = 0.5 / 4;
			else my_vehicle->wheel_turn_speed = 0;
			my_vehicle->if_keep_steer_wheel = false;
			break;
		}

		}

		break;
	}

	} // switch po komunikatach
}

/********************************************************************
FUNKCJA OKNA realizujaca przetwarzanie meldunków kierowanych do okna aplikacji*/
LRESULT CALLBACK WndProc(HWND main_window, UINT message_type, WPARAM wParam, LPARAM lParam)
{

	// PONIŻSZA INSTRUKCJA DEFINIUJE REAKCJE APLIKACJI NA POSZCZEGÓLNE MELDUNKI 


	MessagesHandling(message_type, wParam, lParam);

	switch (message_type)
	{
	case WM_CREATE:  //system_message wysyłany w momencie tworzenia okna
	{

		g_context = GetDC(main_window);

		srand((unsigned)time(NULL));
		int result = GraphicsInitialization(g_context);
		if (result == 0)
		{
			printf("nie udalo sie otworzyc okna graficznego\n");
			//exit(1);
		}

		InteractionInitialisation();

		SetTimer(main_window, 1, 10, NULL);

		return 0;
	}
	case WM_KEYDOWN:
	{
		switch (LOWORD(wParam))
		{
		case VK_F1:  // wywolanie systemu pomocy
		{
			char lan[1024], lan_bie[1024];
			//GetSystemDirectory(lan_sys,1024);
			GetCurrentDirectory(1024, lan_bie);
			strcpy(lan, "C:\\Program Files\\Internet Explorer\\iexplore ");
			strcat(lan, lan_bie);
			strcat(lan, "\\pomoc.htm");
			int wyni = WinExec(lan, SW_NORMAL);
			if (wyni < 32)  // proba uruchominia pomocy nie powiodla sie
			{
				strcpy(lan, "C:\\Program Files\\Mozilla Firefox\\firefox ");
				strcat(lan, lan_bie);
				strcat(lan, "\\pomoc.htm");
				wyni = WinExec(lan, SW_NORMAL);
				if (wyni < 32)
				{
					char lan_win[1024];
					GetWindowsDirectory(lan_win, 1024);
					strcat(lan_win, "\\notepad pomoc.txt ");
					wyni = WinExec(lan_win, SW_NORMAL);
				}
			}
			break;
		}
		case VK_ESCAPE:   // wyjście z programu
		{
			SendMessage(main_window, WM_DESTROY, 0, 0);
			break;
		}
		}
		return 0;
	}

	case WM_PAINT:
	{
		PAINTSTRUCT paint;
		HDC context;
		context = BeginPaint(main_window, &paint);

		DrawScene();
		SwapBuffers(context);

		EndPaint(main_window, &paint);

		return 0;
	}

	case WM_TIMER:

		return 0;

	case WM_SIZE:
	{
		int cx = LOWORD(lParam);
		int cy = HIWORD(lParam);

		WindowSizeChange(cx, cy);

		return 0;
	}

	case WM_DESTROY: //obowiązkowa obsługa meldunku o zamknięciu okna
		if (lParam == 100)
			MessageBox(main_window, "Jest zbyt późno na dołączenie do wirtualnego świata. Trzeba to zrobić zanim inni uczestnicy zmienią jego state.", "Zamknięcie programu", MB_OK);

		EndOfInteraction();
		EndOfGraphics();

		ReleaseDC(main_window, g_context);
		KillTimer(main_window, 1);

		PostQuitMessage(0);
		return 0;

	default: //standardowa obsługa pozostałych meldunków
		return DefWindowProc(main_window, message_type, wParam, lParam);
	}

}

