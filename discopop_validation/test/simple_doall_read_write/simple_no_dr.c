#include <stdio.h>

int main(){
    int arr[10];
    int x = 0;
    int y = 0;
    for(int i=0; i < 10; i++){
        x = i;
        y = x;
    }

    if(x > 3){
        y = y + x;
    }
    else{
        y = y - x;
    }

}