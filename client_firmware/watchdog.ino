void reboot() {
  syslog(F("Rebooting now."));
  delay(100);
  asm volatile ("  jmp 0");  
}

void reboot_after_delay() {
  syslog(F("Delayed reboot initiated."));
  wdt_reset();
  delay(7000);
  wdt_reset();
  delay(7000);
  wdt_reset();
  delay(7000);
  reboot();
}

void enable_watchdog() {
  Serial.print(F("Enabling 8s watchdog..."));
  MCUSR &= ~(1<<WDRF);  //Clear WRDRF way early per the docs
  cli();
  wdt_reset();
  wdt_enable(WDTO_8S);
  WDTCSR = ((1<<WDCE) | (0<<WDE) | (1<<WDIE)); //Enable with interrupt mode
  sei();
  Serial.println(F("Done"));
}

ISR(WDT_vect) {
  // Workaround due to crappy bootloaders
  // We setup our WDT to use interrupt mode and handle the reboot ourselves
  // On the plus side we end up skipping the bootloader wait?
  MCUSR = 0; //reset the status register of the MCU  
  wdt_disable(); //disable the watchdog
  asm volatile ("  jmp 0");  
}
