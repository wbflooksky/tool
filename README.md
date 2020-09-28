# tool
个人使用c++常用的工具类  

int main() {  
    {  
        Timer timer;  
        timer.start();  
        timer.timerEvent(3, 1000, &printContext);  
        Sleep(10 * 1000);  
    }  
}    