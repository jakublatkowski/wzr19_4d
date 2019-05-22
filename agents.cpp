#include <stdlib.h>
#include <time.h>
#include <cmath>
#include "agents.h"
#include <chrono>
#include <algorithm>
#include <windows.h>
#include "graphics.h"
#include "main.h"

extern ViewParameters par_view;

extern float TransferSending(int ID_receiver, int transfer_type, float transfer_value);

using namespace std;

AutoPilot::AutoPilot()
{
}

// ROZSZERZENIE
// metoda do podejmowania decyzji przyjêcia/odrzucenia oferty
// buying = true => decyzja dot. kupna, a nie sprzeda¿y
bool AutoPilot::Decide(bool buying, float my_fuel) {

	if (buying) {
		if (my_fuel > HIGH_THRESHOLD - 10) return false;
		else if (my_fuel < LOW_THRESHOLD) return true;
		else {
			int chances = sqrt(pow(100, 2) - pow((my_fuel - 10) * 10, 2));
			int dice_roll = rand() % 100;
			return dice_roll < chances ? true : false;
		}
	}
	else {
		if (my_fuel < LOW_THRESHOLD + 10) return false;
		else if (my_fuel > HIGH_THRESHOLD) return true;
		else {
			int chances = 10 * (my_fuel - 20);
			int dice_roll = rand() % 100;
			return dice_roll < chances ? true : false;
		}
	}
}

// metoda do obliczania ceny kupna/sprzeda¿y przy publikowaniu oferty
// nim mam wiêcej paliwa, tym taniej sprzedam
// nim mam mniej paliwa, tym za wiêcej kupiê
int AutoPilot::OfferPrice(bool buy_offer, MovableObject *obj) {
	
	int price;
	if (buy_offer) {
		price = min(obj->state.money, BASE_PRICE + (LOW_THRESHOLD - obj->state.amount_of_fuel) * BASE_PRICE);
		sprintf(par_view.inscription1, "AGENT=%d_CHCE_KUPIC_PALIWO_ILOSC=%d_ZA_MONET=%d_", obj->iID, BASE_VOLUME, price);
	}
	else {
		price = max(10, int(BASE_PRICE - obj->state.amount_of_fuel / HIGH_THRESHOLD * BASE_PRICE));
		sprintf(par_view.inscription1, "AGENT=%d,_CHCE_SPRZEDAC_PALIWO_ILOSC=%d_ZA_MONET=%d", obj->iID, BASE_VOLUME, price);
	}
	return price;
}



float distanceFromAToB(Vector3 posA, Vector3 posB)
{
	return sqrtf((posA.x - posB.x)*(posA.x - posB.x) + (posA.y - posB.y)*(posA.y - posB.y) + (posA.z - posB.z)*(posA.z - posB.z));
}

void AutoPilot::AutoControl(MovableObject *obj)
{
	Terrain *_terrain = obj->terrain;

	float pi = 3.1415;
	//podnoszenie_przedm = 1;
	Vector3 vect_local_forward = obj->state.qOrient.rotate_vector(Vector3(1, 0, 0));
	Vector3 vect_local_right = obj->state.qOrient.rotate_vector(Vector3(0, 0, 1));
	// TUTAJ NALE¯Y UMIEŒCIÆ ALGORYTM AUTONOMICZNEGO STEROWANIA POJAZDEM
	// .................................................................
	// .................................................................

	Item ** foundItems;
	long itemsCount = 0;
	float radius = 100;
	Item *nearestItem = nullptr;
	float minDistance;

	while (nearestItem == nullptr)
	{
		itemsCount = _terrain->ItemsInRadius(&foundItems, obj->state.vPos, radius *= 2);
		minDistance = radius / 2;

		for (long i = 0; i < itemsCount; i++)
		{
			if (foundItems[i]->type != ITEM_TREE && foundItems[i]->to_take) {
				if (distanceFromAToB(obj->state.vPos, foundItems[i]->vPos) < minDistance)
				{
					minDistance = distanceFromAToB(obj->state.vPos, foundItems[i]->vPos);
					nearestItem = foundItems[i];
				}
			}
		}
	}

	Vector3 *toItem = new Vector3();

	toItem->x = nearestItem->vPos.x - obj->state.vPos.x;
	toItem->y = nearestItem->vPos.y - obj->state.vPos.y;
	toItem->z = nearestItem->vPos.z - obj->state.vPos.z;

	float tmp = (vect_local_forward ^ *toItem) / (vect_local_forward.length() *toItem->length());
	float x = minDistance;//toItem->length();
	float y = obj->length / 2 - obj->front_axis_dist;

	float alpha = acosf(tmp);//angle_between_vectors2D(vect_local_forward, *toItem);
	obj->state.wheel_turn_angle = - asinf((x * sinf(alpha)) / sqrtf(x*x + y * y - 2 * x*y * cosf(alpha)));

	obj->F = obj->F_max;
	
	//ROZSZERZENIE
	// ustawianie flag w zale¿noœci od obecnej iloœci paliwa i ustawionych górnych/dolnych progów
	if (obj->state.amount_of_fuel > HIGH_THRESHOLD) {
		obj->start_selling = true;
		obj->start_buying = false;
	}
	else if (obj->state.amount_of_fuel < LOW_THRESHOLD) {
		obj->start_selling = false;
		obj->start_buying = true;
	}
	else {
		obj->start_selling = false;
		obj->start_buying = false;
	}
}




void AutoPilot::ControlTest(MovableObject *_ob, float krok_czasowy, float czas_proby)
{
	bool koniec = false;
	float _czas = 0;               // czas liczony od pocz¹tku testu
	//FILE *pl = fopen("test_sterowania.txt","w");
	while (!koniec)
	{
		_ob->Simulation(krok_czasowy);
		AutoControl(_ob);
		_czas += krok_czasowy;
		if (_czas >= czas_proby) koniec = true;
		//fprintf(pl,"czas %f, vPos[%f %f %f], got %d, pal %f, F %f, wheel_turn_angle %f, breaking_degree %f\n",_czas,_ob->vPos.x,_ob->vPos.y,_ob->vPos.z,_ob->money,_ob->amount_of_fuel,_ob->F,_ob->wheel_turn_angle,_ob->breaking_degree);
	}
	//fclose(pl);
}

// losowanie liczby z rozkladu normalnego o zadanej sredniej i wariancji
float Randn(float srednia, float wariancja, long liczba_iter)
{
	//long liczba_iter = 10;  // im wiecej iteracji tym rozklad lepiej przyblizony
	float suma = 0;
	for (long i = 0; i < liczba_iter; i++)
		suma += (float)rand() / RAND_MAX;
	return (suma - (float)liczba_iter / 2)*sqrt(12 * wariancja / liczba_iter) + srednia;
}