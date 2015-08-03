#ifndef ARIFTCONTROL_H
#define ARIFTCONTROL_H
#include "IDSuEyeInputHandler.h"
#include "../include/Shader.h"
#include <direct.h>

#define CAM1 1
#define CAM2 2
#define RIFT_RESOLUTION_WIDTH 1280
#define RIFT_RESOLUTION_HEIGHT 800
//#define RIFT_RESOLUTION_WIDTH (1122 * 2)
//#define RIFT_RESOLUTION_HEIGHT 1553

class GraphicsAPI;

class ARiftControl
{
  public:
    enum InputMode { DEFAULT = 0, MODEL = 1, WORLD = 2, CAMERA = 3 };

    ARiftControl();
    virtual ~ARiftControl();
    void init();
    void getImages();

    void handleKey(char key);
    void hanldeFlip();
    void handleSave();
    void handleCameraAutoFeatures();

    void start() { running_ = true; };
    void stop() { running_ = false; };
    bool keepRunning() { return running_; };

    std::string base_save_name_;


		float virtualcameraX_translation_ = 0.0f;
		float virtualcameraY_translation_ = 0.0f;
		float virtualcameraZ_translation_ = 0.0f;

    int wait_time_ = 30;

    IDSuEyeInputHandler *cam_input = NULL;
    Shader::UndistortionBuffer left_cam_params_;
    Shader::UndistortionBuffer right_cam_params_;

    void ResetModelChange();
    float model_offset_x_ = 0.0f;
    float model_offset_y_ = 0.0f;
    float model_offset_z_ = 0.0f;

    float world_offset_x_ = 0.0f;
    float world_offset_y_ = -0.26f;
    float world_offset_z_ = 2.9f;

    float camera_offset_x_ = 0.0f;
    float camera_offset_y_ = 0.0f;
    float camera_offset_z_ = 0.0f;
    bool show_eye_pose_ = false;
    float ipd_offset_ = 0.0f;
    bool print_eye_dist_ = false;
    int cam_id_ = 1;
    int identify_cam_ = 0;
    bool show_left_cam_ = false;

    bool changed_model_ = false;
    float model_rotation_ = 0.0f;
    bool model_auto_rotate_ = false;
    bool model_auto_translate_ = false;
    GraphicsAPI* graphics_api_ = NULL;
    InputMode input_mode_ = InputMode::DEFAULT;
  protected:
  private:
    bool running_ = false;
    int write_counter_ = 0;
    float step_ = 0.2f;
    char last_key_ = 0;
};

#endif // ARIFTCONTROL_H
