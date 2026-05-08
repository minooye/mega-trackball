/*
 * Mega Drive Trackball - Mega Mouse emulator for Arduino Nano
 * 
 * Reads a quadrature arcade trackball and emulates a Sega Mega Mouse
 * on the DB-9 port of the Sega Mega Drive / Genesis.
 * 
 * Target: ATmega168PA-AU or ATmega328P at 16 MHz (Arduino Nano)
 * 
 * See docs/WIRING.md in the repo for wiring details.
 */

// ====================================================================
// Pre-computed nibble tables for the 9-nibble Mega Mouse sequence
// portd_tab[i] : value for PORTD bits 4-7 (TL + D0..D2)
// portb_tab[i] : value for PORTB bit 0 (D3)
// ====================================================================

volatile uint8_t portd_tab[9] = {
  0x70,  // i=0 : nibble=0xB (1011) TL=1   (mouse signature)
  0xE0,  // i=1 : nibble=0xF (1111) TL=0   (header, ignored)
  0xF0,  // i=2 : nibble=0xF (1111) TL=1   (header, ignored)
  0x00,  // i=3 : nibble=0x0       TL=0   (signs / overflow)
  0x10,  // i=4 : nibble=0x0       TL=1   (buttons)
  0x00,  // i=5 : nibble=0x0       TL=0   (X MSN)
  0x10,  // i=6 : nibble=0x0       TL=1   (X LSN)
  0x00,  // i=7 : nibble=0x0       TL=0   (Y MSN)
  0x10   // i=8 : nibble=0x0       TL=1   (Y LSN)
};
volatile uint8_t portb_tab[9] = { 1, 1, 1, 0, 0, 0, 0, 0, 0 };

volatile uint8_t nibbleIndex = 0;

// ====================================================================
// Quadrature counters (modified by PCINT)
// ====================================================================

volatile int16_t xCounter = 0;
volatile int16_t yCounter = 0;
volatile uint8_t prevX = 0;
volatile uint8_t prevY = 0;

// ====================================================================
// ISR : Mega Mouse 3-line handshake
// INT0 (D2) = TH, INT1 (D3) = TR
// ====================================================================

ISR(INT0_vect) {
  if (PIND & (1 << 2)) {
    // TH=1 : idle - TL=1, D=0
    PORTD = (PORTD & 0x0F) | 0x10;
    PORTB &= ~(1 << 0);
  } else {
    // TH=0 : transaction start, present nibble 0 = 0xB
    PORTD = (PORTD & 0x0F) | 0x70;
    PORTB |= (1 << 0);
  }
  nibbleIndex = 0;
}

ISR(INT1_vect) {
  // Race-condition guard: if TH=1 (idle), don't advance
  if (PIND & (1 << 2)) return;
  
  uint8_t i = nibbleIndex;
  if (i < 8) {
    i++;
    PORTD = (PORTD & 0x0F) | portd_tab[i];
    if (portb_tab[i])
      PORTB |= (1 << 0);
    else
      PORTB &= ~(1 << 0);
    nibbleIndex = i;
  }
}

// ====================================================================
// ISR : trackball quadrature decoder (PCINT0)
// Pins: D9 (PB1), D10 (PB2), D11 (PB3), D12 (PB4)
// ====================================================================

ISR(PCINT0_vect) {
  uint8_t pinb = PINB;
  uint8_t curX = (pinb >> 1) & 0x03;  // PB1 + PB2
  uint8_t curY = (pinb >> 3) & 0x03;  // PB3 + PB4
  
  // Decode direction from transition (2-bit Gray code)
  uint8_t xTrans = (prevX << 2) | curX;
  switch (xTrans) {
    case 0b0001: case 0b0111: case 0b1110: case 0b1000:
      xCounter++; break;
    case 0b0010: case 0b1011: case 0b1101: case 0b0100:
      xCounter--; break;
  }
  prevX = curX;
  
  uint8_t yTrans = (prevY << 2) | curY;
  switch (yTrans) {
    case 0b0001: case 0b0111: case 0b1110: case 0b1000:
      yCounter++; break;
    case 0b0010: case 0b1011: case 0b1101: case 0b0100:
      yCounter--; break;
  }
  prevY = curY;
}

// ====================================================================
// Updates the nibble tables with new deltas
// dx, dy : signed deltas (-128 to +127)
// ====================================================================

void setNibbles(int8_t dx, int8_t dy) {
  uint8_t signs = 0;
  uint8_t absX, absY;
  
  if (dx < 0) { signs |= 0x01; absX = (uint8_t)(-dx); }
  else        { absX = (uint8_t)dx; }
  
  if (dy < 0) { signs |= 0x02; absY = (uint8_t)(-dy); }
  else        { absY = (uint8_t)dy; }
  
  uint8_t nibbles[9] = {
    0xB, 0xF, 0xF,                       // ID + headers
    signs,                                // signs / overflow
    0x0,                                  // buttons (extend if needed)
    (uint8_t)(absX >> 4),                // X MSN
    (uint8_t)(absX & 0x0F),              // X LSN
    (uint8_t)(absY >> 4),                // Y MSN
    (uint8_t)(absY & 0x0F)               // Y LSN
  };
  
  // Rebuild tables with proper TL pattern
  for (uint8_t i = 0; i < 9; i++) {
    uint8_t n = nibbles[i];
    uint8_t tl = (i == 0) ? 1 : !(i & 1);  // 1,0,1,0,1,0,1,0,1
    
    uint8_t pd = 0;
    if (tl)        pd |= (1 << 4);
    if (n & 0x01)  pd |= (1 << 5);
    if (n & 0x02)  pd |= (1 << 6);
    if (n & 0x04)  pd |= (1 << 7);
    portd_tab[i] = pd;
    portb_tab[i] = (n & 0x08) ? 1 : 0;
  }
}

// ====================================================================
// Setup
// ====================================================================

void setup() {
  // Mega Mouse protocol pins set to idle as soon as possible
  // D2-D3 (TH/TR) input, D4-D7 (TL/D0/D1/D2) output
  DDRD = (DDRD & 0b00001111) | 0b11110000;
  // D8 (PB0) output (D3 of the protocol)
  DDRB |= (1 << 0);
  // Initial idle state: TL=1, D=0
  PORTD = (PORTD & 0b00001111) | (1 << 4);
  PORTB &= ~(1 << 0);
  
  // Trackball quadrature pins (D9-D12 = PB1-PB4) input with pull-up
  DDRB &= ~((1 << 1) | (1 << 2) | (1 << 3) | (1 << 4));
  PORTB |= (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4);
  
  // Disable parasitic interrupts (Timer1, Timer2, USART, ADC)
  // Keep Timer0 active for millis()
  TIMSK1 = 0;
  TIMSK2 = 0;
  UCSR0B = 0;
  ADCSRA = 0;
  
  // LED L (D13) on as a status indicator
  DDRB |= (1 << 5);
  PORTB |= (1 << 5);
  
  // Initialize quadrature state
  uint8_t pinb = PINB;
  prevX = (pinb >> 1) & 0x03;
  prevY = (pinb >> 3) & 0x03;
  
  // Initialize tables with zero deltas
  setNibbles(0, 0);
  
  // Enable PCINT0 on PB1-PB4
  PCICR = (1 << PCIE0);
  PCMSK0 = (1 << PCINT1) | (1 << PCINT2) | (1 << PCINT3) | (1 << PCINT4);
  
  // Enable INT0 and INT1 on any change
  EICRA = (EICRA & 0b11110000) | 0b00000101;
  EIMSK |= (1 << INT0) | (1 << INT1);
  EIFR = (1 << INTF0) | (1 << INTF1);
  sei();
}

// ====================================================================
// Loop : at 60 Hz, send accumulated deltas to the protocol
// ====================================================================

void loop() {
  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate < 16) return;  // ~60 Hz
  lastUpdate = millis();
  
  // Atomically read and reset counters
  cli();
  int16_t dx = xCounter;
  int16_t dy = yCounter;
  xCounter = 0;
  yCounter = 0;
  sei();
  
  // Saturate to Mega Mouse format (s8)
  if (dx > 127)  dx = 127;
  if (dx < -128) dx = -128;
  if (dy > 127)  dy = 127;
  if (dy < -128) dy = -128;
  
  // Update nibble tables (ISRs will read these tables)
  cli();
  setNibbles((int8_t)dx, (int8_t)dy);
  sei();
}
