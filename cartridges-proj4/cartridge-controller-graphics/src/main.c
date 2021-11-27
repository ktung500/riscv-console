#include <stdint.h>
#include <string.h>
#include "../include/RVCOS.h"

#define DEFAULT_COLOR_WHITE     0x0F
#define DEFAULT_COLOR_BLACK     0x10
#define DEFAULT_COLOR_TEAL      0x06
#define SCREEN_WIDTH            0x200
#define SCREEN_HEIGHT           0x120
#define SPRITE_WIDTH            0x10
#define SPRITE_HEIGHT           0x10

void WriteString(const char *str){
    const char *Ptr = str;
    while(*Ptr){
        Ptr++;
    }
    RVCWriteText(str,Ptr-str);
}


int main() {
    SControllerStatus ControllerStatus;
    TGraphicID Background, Cursor;
    SGraphicPosition Position;
    SGraphicDimensions Dimensions;
    TPaletteIndex PixeBuffer[SCREEN_WIDTH];
    TPaletteIndex CurrentColor = DEFAULT_COLOR_TEAL;
    for(int Index = 0; Index < SCREEN_WIDTH; Index++){
        PixeBuffer[Index] = DEFAULT_COLOR_WHITE;
    }
    RVCGraphicCreate(RVCOS_GRAPHIC_TYPE_FULL,&Background);
    Position.DXPosition = 0;
    Dimensions.DHeight = 1;
    Dimensions.DWidth = SCREEN_WIDTH;
    WriteString("first creates done\n");
    for(Position.DYPosition = 0; Position.DYPosition < SCREEN_HEIGHT; Position.DYPosition++){
        RVCGraphicDraw(Background,&Position,&Dimensions,PixeBuffer,SCREEN_WIDTH);
    }
    Position.DXPosition = 0;
    Position.DYPosition = 0;
    Position.DZPosition = 0;
    WriteString("draws done\n");
    RVCGraphicActivate(Background,&Position,NULL,RVCOS_PALETTE_ID_DEFAULT);
    WriteString("activate done\n");
    for(int Index = 0; Index < SCREEN_WIDTH; Index++){
        PixeBuffer[Index] = CurrentColor;
    }
    RVCGraphicCreate(RVCOS_GRAPHIC_TYPE_SMALL,&Cursor);
    WriteString("second create done\n");
    Position.DXPosition = 0;
    Position.DYPosition = 0;
    Dimensions.DHeight = SPRITE_HEIGHT;
    Dimensions.DWidth = SPRITE_WIDTH;
    WriteString("second draw called\n");
    RVCGraphicDraw(Cursor,&Position,&Dimensions,PixeBuffer,SPRITE_WIDTH);
    WriteString("second draw done\n");
    Position.DXPosition = 0;
    Position.DYPosition = 0;
    Position.DZPosition = 4;
    RVCGraphicActivate(Cursor,&Position,&Dimensions,RVCOS_PALETTE_ID_DEFAULT);
    WriteString("before changing modes\n");
    RVCChangeVideoMode(RVCOS_VIDEO_MODE_GRAPHICS);
    //RVCChangeVideoMode(1);
    WriteString("mode changed\n");
    while(1){
        RVCWriteText("\x1B[H",3);
        RVCReadController(&ControllerStatus);
        if(ControllerStatus.DUp){
            if(Position.DYPosition){
                Position.DYPosition--;
                RVCGraphicActivate(Cursor,&Position,&Dimensions,RVCOS_PALETTE_ID_DEFAULT);
            }
        }
        else if(ControllerStatus.DRight){
            if(Position.DXPosition + SPRITE_WIDTH < SCREEN_WIDTH){
                Position.DXPosition++;
                RVCGraphicActivate(Cursor,&Position,&Dimensions,RVCOS_PALETTE_ID_DEFAULT);
            }
        }
        else if(ControllerStatus.DLeft){
            if(Position.DXPosition){
                Position.DXPosition--;
                RVCGraphicActivate(Cursor,&Position,&Dimensions,RVCOS_PALETTE_ID_DEFAULT);
            }
        }
        else if(ControllerStatus.DDown){
            if(Position.DYPosition + SPRITE_HEIGHT < SCREEN_HEIGHT){
                Position.DYPosition++;
                RVCGraphicActivate(Cursor,&Position,&Dimensions,RVCOS_PALETTE_ID_DEFAULT);
            }
        }
        else if(ControllerStatus.DButton1){
            CurrentColor++;
            for(int Index = 0; Index < SCREEN_WIDTH; Index++){
                PixeBuffer[Index] = CurrentColor;
            }
            SGraphicPosition TempPosition = {0,0,0};
            RVCGraphicDraw(Cursor,&TempPosition,&Dimensions,PixeBuffer,SPRITE_WIDTH);
            RVCGraphicActivate(Cursor,&Position,&Dimensions,RVCOS_PALETTE_ID_DEFAULT);
        }
        else if(ControllerStatus.DButton2){
            CurrentColor--;
            for(int Index = 0; Index < SCREEN_WIDTH; Index++){
                PixeBuffer[Index] = CurrentColor;
            }
            SGraphicPosition TempPosition = {0,0,0};
            RVCGraphicDraw(Cursor,&TempPosition,&Dimensions,PixeBuffer,SPRITE_WIDTH);
            RVCGraphicActivate(Cursor,&Position,&Dimensions,RVCOS_PALETTE_ID_DEFAULT);
        }
        else if(ControllerStatus.DButton3){
            RVCGraphicDeactivate(Cursor);
        }
        else if(ControllerStatus.DButton4){
            RVCGraphicActivate(Cursor,&Position,&Dimensions,RVCOS_PALETTE_ID_DEFAULT);
        }
    }
    return 0;
}
