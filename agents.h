#ifndef _OBJECTS__H
#include "objects.h"
#endif

//ROZSZERZENIE
// pr�g dolny/g�rny kiedy agenty zaczynaj� publikowa� oferty kupna/sprzeda�y
#define LOW_THRESHOLD 10
#define HIGH_THRESHOLD 30

// bazowa cena jednej "beczu�ki" paliwa
#define BASE_PRICE 50
// bazowa ilo�� paliwa w jednej "beczu�ce"
#define BASE_VOLUME 5
// ile ms agent czeka, zanim znowu publikuje ofert�
#define OFFER_TIME 5000
// minimalna cena sprzeda�y paliwa, poni�ej czego agent nigdy nie zejdzie
#define MIN_PRICE 10



class AutoPilot
{       
private:
	//float par[100]; // parametry pocz�tkowe i parametry aktualne
	//long number_of_params;
public:
  AutoPilot();

  //ROZSZERZENIE
  // metoda do podejmowania decyzji przyj�cia/odrzucenia oferty
  static bool Decide(bool buying, float my_fuel);

  // metoda do obliczania ceny kupna/sprzeda�y przy publikowaniu oferty
  static int OfferPrice(bool buy_offer, MovableObject *obj);



  void AutoControl(MovableObject *ob);                        // pojedynczy krok sterowania
  void ControlTest(MovableObject *_ob,float krok_czasowy, float czas_proby); 
};