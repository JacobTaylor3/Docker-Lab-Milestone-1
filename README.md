Introduction:

- First, you need to have WSL (if on Windows). Please perform all of the below actions in the same terminal (WSL preferably) on your local machine.
1. From there, do git clone https://github.com/JacobTaylor3/Docker-Lab-Milestone-2.git
2. After this, download the virtual machine, which is linked here https://drive.google.com/drive/folders/1CZh9q3QzyJD2nV0JFkTGE0_XIAlg1qUv?usp=sharing 
3. After this, please upload this to VirtualBox and then go to file->tools->network and select the IPv4 address (address only) you have by default (screenshot is attached of mine)
4. After this, please run Docker Desktop on your local machine, and keep it open.
5. After this, please run "sudo ./launch.sh" in the terminal. When it asks for IP input, please input the IPv4 address from step 3.
6. Next, the docker containers will spin up, which may take ~5 minutes. This will start the vulnerable website and web server hosting the implant executable.
7. After this, in order to attach to the controller executable Docker image in the current terminal, please run the command specified in the output (i.e. sudo docker attach c2-server). Then, hit enter.
8. After this, please load up the virtual machine. The password is "victim". And then go to the web address specified in the terminal output. 
9. After the implant is downloaded and run, it will connect via sockets to the controller executable (socket binded here--listener). Now, you will be able to send data to the implant via the controller in your terminal.


