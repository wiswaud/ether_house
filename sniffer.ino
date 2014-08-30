//callback that prints received packets to the serial port
void packet_sniffer_callback(uint8_t srcmacaddr[6], byte ip[4], const char *data, word len) {
  Serial.print("Got Sniffed Packet from: ");
//  Serial.print("Src ip:  "); 
  ether.printIp(ip);
  Serial.println();
  /*
 Serial.print("Src Mac: "); 
  printMac(srcmacaddr);
  Serial.print("Data:    "); 
  Serial.println(data);
  Serial.print("Size:    "); 
  Serial.println(len);
  Serial.println();
  // Save the target ip for next time.
  Serial.println("Saving source ip as our new target ip");
  */
  memcpy(target_ip, ip, sizeof target_ip);
}

