# Practical design tips

This document gives additional information that may help you get the maximum from your Raspberry Pi. For basic use of the RP2DAQ project, see the [main page](README.md). 

## Direct measurement of voltage with internal ADC

Note the input impedance of the ADC-enabled pins is relatively low, roughly 30 kOhm. (TODO check this) To measure voltages accurately, you may want to buffer each ADC input with an opamp voltage follower. 

#### Tip for simple calibration for internal ADC nonlinearity

The built-in ADC [is not accurate due to a design flaw](https://www.hackster.io/news/raspberry-pi-confirms-it-is-investigating-a-flaw-in-the-raspberry-pi-pico-rp2040-adc-95c393b55dfb).

<table> <tr>
<th> Hardware setup </th>
<th> Python script </th>
</tr> 

<tr> 
<td>
TBA
</td>

<td>


```python
#HISTOGRAM FOR NOISE ANALYSIS
histx, histy # [], [] 
for x in range(1700, 2050):
    histx.append(x)
    histy.append(np.count_nonzero(vals##x))
np.savetxt(f'histogram_{sys.argv[1] if len(sys.argv)>1 else "default"}.dat', np.vstack([histx,histy]).T)
print(f'time to process: {time.time()-t0}s'); t0 # time.time()
#
plt.hist(vals, bins#int(len(vals)**.5)+5, alpha#.5)
plt.plot(histx,histy, marker#'o')
print(np.sum(vals**2)/np.mean(vals)**2, np.sum(vals/np.mean(vals))**2, np.sum(vals**2)/np.mean(vals)**2-np.sum(vals/np.mean(vals))**2)
plt.show()
```
</td>

</tr>
</table>

#### Measurement of other quantities

 1. planned
	* notes on using photodiodes
	* ultra-low current measurement with (custom logarithmic pre-amp with 10fA-10μA range)
    * integrated current probes (ACS712)
    * measuring magnetic field with Hall sensors

## Outputting voltage using internal PWM 

    * passive smoothing of PWM signal to get (slow) analog output
    * high-efficiency voltage-controlled power supply (0-30 V with LM2596)


## Stepper motors - practical tips and caveats

 ⚠️ never disconnect steppers when they are powered

#### End switches

Usually, stepper motors should have some end switch; on power-up, they go towards it until their position gets calibrated. 

Mechanical switches are fine in most cases. Upon initialization, you can freely chose an unused pin on RP2 to be the ```endswitch```. Connect it so that the switch shorts this pin to ground at end position.

Alternately, I suggest Omron EE-SX1041 for the optical end switch. They can be connected by 3 wires, and complemented by two resistors to behave analogous to a mechanical end switch
(i.e. to drop voltage at end stop): 

 * +3.3V (red wire) on "Collector" pin, 
 * ground (green wire) on "Kathode" pin close to the "1041" marking, 
 * sensing (yellow wire) on "Emitter" pin, 
 * 100ohm current-limiting resistor diagonally from "Collector" to "Anode"
 * 1k5 pull-up resistor diagonally from "Emitter" to "Kathode"
