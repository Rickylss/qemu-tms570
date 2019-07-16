#ifndef APP_H_
#define APP_H_
#define APPNAMELENGTH   256
#define APPMAXCOUNT    30
typedef struct {
    char appname[APPNAMELENGTH];
    uint32_t appaddr;
}APPinfo;

extern APPinfo app[APPMAXCOUNT];
extern int appcount;
extern uint32_t apptestaddr;


#endif