#ifndef _OBJECTS__H
#include "objects.h"
#endif

#define LOW_THRESHOLD 10
#define HIGH_THRESHOLD 30
#define BASE_PRICE 50
#define BASE_VOLUME 5
#define OFFER_TIME 5000
#define MIN_PRICE 10

class AutoPilot
{       
private:
	//float par[100]; // parametry pocz¹tkowe i parametry aktualne
	//long number_of_params;
public:
  AutoPilot();
  static bool Decide(bool buying, float my_fuel);
  //void DebugDecide(float my_fuel);
  static int OfferPrice(bool buy_offer, MovableObject *obj);
  void AutoControl(MovableObject *ob);                        // pojedynczy krok sterowania
  void ControlTest(MovableObject *_ob,float krok_czasowy, float czas_proby); 
};