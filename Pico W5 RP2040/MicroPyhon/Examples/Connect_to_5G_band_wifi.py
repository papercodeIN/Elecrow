from machine import UART, Pin
import time

# Initialize UART1 with baudrate 115200, transmit on Pin(4), receive on Pin(5)
uart1 = UART(1, baudrate=115200, tx=Pin(4), rx=Pin(5))

# Function to send AT commands using UART1
def send_at_command(uart, command):
    """Send an AT command over UART and notify the user."""
    print(f"Sending AT command: {command}")
    uart1.write(command + '\n')  # Send the AT command followed by a newline
    time.sleep(0.1)  # Short delay to allow the module to process the command

# Function to continuously receive data from UART1 until a newline character is detected
def Uart1_receive():
    """Receive data from UART and display it clearly."""
    data = b''  # Initialize an empty bytes object to store incoming data
    succeed = "OK"  # Expected success response from the module
    fail = "ERROR"  # Expected error response from the module

    print("Waiting for response from the module...")
    while True:
        if uart1.any():  # Check if there is data available to read
            data += uart1.read()  # Append the received data
            
        if data.endswith(b'\n'):  # Check if the data ends with a newline character
            try:
                decoded_data = data.decode('utf-8').strip()  # Decode the data for better readability
            except UnicodeDecodeError:
                decoded_data = data  # Use raw bytes if decoding fails

            if succeed in decoded_data:  # If the success response is in the data
                print(f"Response: {decoded_data} (Command executed successfully!)")
                break  # Exit the loop
            elif fail in decoded_data:  # If the error response is in the data
                print(f"Response: {decoded_data} (Command failed!)")
                break  # Exit the loop
            else:
                print(f"Response so far: {decoded_data}")  # Print partial data

        time.sleep(0.5)  # Wait for 0.5 seconds before continuing

# Send an AT command to set the module's working mode
print("Configuring Wi-Fi working mode...")
send_at_command(uart1, 'AT+WMODE=3,1') 
Uart1_receive()

# Send an AT command to connect the module to a Wi-Fi network
wifi_ssid = ""
wifi_password = ""
print(f"Connecting to Wi-Fi network: {wifi_ssid}...")
send_at_command(uart1, f'AT+WJAP={wifi_ssid},{wifi_password}')
Uart1_receive()

