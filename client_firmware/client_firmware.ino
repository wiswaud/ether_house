#include <EEPROM.h>
#include <EtherCard.h>
#include <avr/wdt.h>

#define CS_PIN 10

#define SECONDS 1000L
#define MINUTES (60 * SECONDS)
#define HOURS (60 * MINUTES)
// NOTE: All of these timers are in Milliseconds!
// Ping our target every 2 seconds, if we haven't seen traffic from it
#define PINGER_INTERVAL (2 * SECONDS)
// If target IP isn't known, ping sweep this often
#define PINGSWEEP_INTERVAL_FIND_TARGET (1 * MINUTES)
// Ping everything every hour
#define PINGSWEEP_INTERVAL_RESCAN (1 * HOURS)
// How often to check back to the server for sync updates
#define SYNC_INTERVAL (5 * MINUTES)
// General timeout for API calls and such. Needs to be lower than the 8s watchdog!
#define HTTP_TIMEOUT (5 * SECONDS)
// A device is gone if we haven't heard from them in 15 minutes
#define ABSENSE_TIMEOUT (15 * MINUTES)
// Reboot the entire thing every 24 hours for good measure.
#define REBOOT_INTERVAL (24 * HOURS)

#define NUM_HOUSES 8
#define MY_ID 0
#define MY_ID_CHAR "0"
#define MY_API_KEY "testkey"

#define BYTETOBINARYPATTERN "%d%d%d%d%d%d%d%d"
#define BYTETOBINARY(byte)  \
(byte & 0x80 ? 1 : 0), \
  (byte & 0x40 ? 1 : 0), \
  (byte & 0x20 ? 1 : 0), \
  (byte & 0x10 ? 1 : 0), \
  (byte & 0x08 ? 1 : 0), \
  (byte & 0x04 ? 1 : 0), \
  (byte & 0x02 ? 1 : 0), \
  (byte & 0x01 ? 1 : 0)

const uint8_t my_mac[] = {
  0x74,0x69,0x69,0x2D,0x30,MY_ID };
const uint8_t allZeros[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00 };
const uint8_t allOnes[] = {
  0xFF, 0xFF, 0xFF, 0xFF };
const char api_server[] PROGMEM = "etherhouse.xkyle.com";

uint8_t target_mac[6] = {
  -1,-1,-1,-1,-1,-1 };
uint8_t target_ip[4] = {
  255, 255, 255, 255 };
uint8_t api_ip[4];
uint8_t state = 0; //No houses on at first
uint8_t Ethernet::buffer[500];

static long pinger_timer;
static long absense_timer;
static long pingsweep_timer;
static long sync_timer;

volatile bool locked = false;

void setup () {
  Serial.begin(115200);
  enable_watchdog();
  Serial.println(F("\nether_house"MY_ID_CHAR" starting network configuration"));
  setup_pins();
  readStateFromEeprom();

  wdt_reset();
  if (ether.begin(sizeof Ethernet::buffer, my_mac, CS_PIN) == 0) {
    Serial.println(F("Failed to access Ethernet controller"));
    reboot();
  }

  wdt_reset();
  if (!ether.dhcpSetup()) {
    syslog(F("DHCP failed"));
    reboot();
  }

  wdt_reset();
  if (!ether.dnsLookup(api_server)) {
    syslog(F("DNS failed"));
    reboot_after_delay();
  }
  memcpy(api_ip, ether.hisip, sizeof api_ip);

  print_netcfg();

  syslog(F("etherhouse"MY_ID_CHAR" booted!"));

  wdt_reset();
  get_target_mac();
  wdt_reset();
  get_remote_state();
  wdt_reset();

  Serial.println(F("Finished initial configuration"));
  Serial.println(F("Now entering main loop\n"));

  // Setup timers
  pinger_timer = millis();
  // Start the absense timer with the total grace period to give it the benifit of the doubt
  absense_timer = millis();
  // We can start the ping sweep on bootup.
  pingsweep_timer = millis() - PINGSWEEP_INTERVAL_FIND_TARGET;
  // We already got the state from above. Setup the next issue.
  sync_timer = millis();

  Ethernet::enableBroadcast();
  Ethernet::enableMulticast();
}

void loop () {
  long pingsweep_interval;

  wdt_reset();
  // Normal loop of getting packets if they are available
  ether.packetLoop(ether.packetReceive());
  wdt_reset();

  // Ping our target to see if they are alive
  if (millis() > pinger_timer + PINGER_INTERVAL) {
    pinger_timer = millis();
    if (millis() > absense_timer + PINGER_INTERVAL)
      ping_target();
  }

  // If we haven't heard from our device, time to time out and turn off
  // The sniffer callback resets the absense_timer
  if (bitRead(state, MY_ID) && (millis() > absense_timer + ABSENSE_TIMEOUT)) {
    // At the last second, let's try a last-ditch ping sweep.
    pingsweep_timer = millis();
    ping_sweep();
    wdt_reset();
    // Then if we are *still* here
    if (bitRead(state, MY_ID) && (millis() > absense_timer + ABSENSE_TIMEOUT)) {
      syslog(F("Absense timeout of target. Turning off light "MY_ID_CHAR));
      turn_my_house_off();
      // Start looking for new target_ip; the DHCP reservation might time out
      // and the device get a new address next time.
      memcpy(target_ip, allOnes, sizeof target_ip);
      saveStateToEeprom();
    }
  }

  // If we don't yet know the target_ip, ping sweep fairly often to find it. Otherwise:
  // After a long time we ping everything in case we don't even know what ip our device has
  if (!memcmp(target_ip, allOnes, sizeof target_ip))
    pingsweep_interval = PINGSWEEP_INTERVAL_FIND_TARGET;
  else
    pingsweep_interval = PINGSWEEP_INTERVAL_RESCAN;
  if (millis() > pingsweep_timer + pingsweep_interval) {
    pingsweep_timer = millis();
    ping_sweep();
  }

  if (millis() > sync_timer + SYNC_INTERVAL) {
    sync_timer = millis();
    get_remote_state();
  }

  if (millis() > REBOOT_INTERVAL) {
    syslog(F("Rebooting after 24 hours"));
    reboot();
  }
}
