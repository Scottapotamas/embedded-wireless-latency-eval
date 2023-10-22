bool trigger_pending = false;

void setup( void )
{
  // Output LED
  pinMode(PB0, OUTPUT);

  // Input IO
  attachInterrupt( digitalPinToInterrupt(PA0), 
                   externalTrigger, 
                   RISING );
}

void externalTrigger( void )
{
  trigger_pending = true;
}

void loop( void )
{

  if( trigger_pending )
  {
    digitalWrite(PB0, HIGH);
    trigger_pending = false;
  }
  else
  {
    digitalWrite(PB0, LOW);
  }

}
