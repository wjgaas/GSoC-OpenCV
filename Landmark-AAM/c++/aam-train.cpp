/*----------------------------------------------
 * Usage:
 * facemark_demo_aam <face_cascade_model> <eyes_cascade_model> <aam_model> <training_images> <annotation_files> [test_files]
 *
 * Example:
 * facemark_demo_aam models/face_cascade.xml models/eyes_cascade.xml models/AAM.yaml trian/images_train.txt train/points_train.txt
 *
 * Notes:
 * the user should provides the list of training images_train
 * accompanied by their corresponding landmarks location in separated files.
 * example of contents for images_train.txt:
 * ../trainset/image_0001.png
 * ../trainset/image_0002.png
 * example of contents for points_train.txt:
 * ../trainset/image_0001.pts
 * ../trainset/image_0002.pts
 * where the image_xxxx.pts contains the position of each face landmark.
 * example of the contents:
 *  version: 1
 *  n_points:  68
 *  {
 *  115.167660 220.807529
 *  116.164839 245.721357
 *  120.208690 270.389841
 *  ...
 *  }
 * example of the dataset is available at https://ibug.doc.ic.ac.uk/download/annotations/lfpw.zip
 *--------------------------------------------------*/

#include <stdio.h>
#include <fstream>
#include <sstream>
#include "opencv2/core.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/face.hpp"

#include <iostream>
#include <string>
#include <ctime>

using namespace std;
using namespace cv;
using namespace cv::face;

bool myDetector(InputArray image, OutputArray faces, CascadeClassifier *face_cascade)
{
    Mat gray;
    if (image.channels() > 1)
        cvtColor(image, gray, COLOR_BGR2GRAY);
    else
        gray = image.getMat().clone();

    equalizeHist(gray, gray);

    vector<Rect> faces_;
    face_cascade->detectMultiScale(gray, faces_, 1.4, 2, CASCADE_SCALE_IMAGE, Size(30, 30));
    Mat(faces_).copyTo(faces);
    return true;
}

bool getInitialFitting(Mat image, Rect face, vector<Point2f> s0, CascadeClassifier eyes_cascade, Mat & R, Point2f & Trans, float & scale){
    vector<Point2f> mybase;
    vector<Point2f> T;
    vector<Point2f> base = Mat(Mat(s0)+Scalar(image.cols/2,image.rows/2)).reshape(2);

    vector<Point2f> base_shape,base_shape2 ;
    Point2f e1 = Point2f((float)((base[39].x+base[36].x)/2.0),(float)((base[39].y+base[36].y)/2.0)); //eye1
    Point2f e2 = Point2f((float)((base[45].x+base[42].x)/2.0),(float)((base[45].y+base[42].y)/2.0)); //eye2

    if(face.width==0 || face.height==0) return false;

    vector<Point2f> eye;
    bool found=false;

    Mat faceROI = image( face);
    vector<Rect> eyes;

    // In each face, detect eyes
    eyes_cascade.detectMultiScale( faceROI, eyes, 1.1, 2, CASCADE_SCALE_IMAGE, Size(20, 20) );
    if(eyes.size()==2){
        found = true;
        int j=0;
        Point2f c1( (float)(face.x + eyes[j].x + eyes[j].width*0.5), (float)(face.y + eyes[j].y + eyes[j].height*0.5));

        j=1;
        Point2f c2( (float)(face.x + eyes[j].x + eyes[j].width*0.5), (float)(face.y + eyes[j].y + eyes[j].height*0.5));

        Point2f pivot;
        double a0,a1;
        if(c1.x<c2.x){
            pivot = c1;
            a0 = atan2(c2.y-c1.y, c2.x-c1.x);
        }
        else{
            pivot = c2;
            a0 = atan2(c1.y-c2.y, c1.x-c2.x);
        }

        scale = (float)(norm(Mat(c1)-Mat(c2))/norm(Mat(e1)-Mat(e2)));

        mybase= Mat(Mat(s0)*scale).reshape(2);
        Point2f ey1 = Point2f((float)((mybase[39].x+mybase[36].x)/2.0),(float)((mybase[39].y+mybase[36].y)/2.0));
        Point2f ey2 = Point2f((float)((mybase[45].x+mybase[42].x)/2.0),(float)((mybase[45].y+mybase[42].y)/2.0));


        #define TO_DEGREE 180.0/3.14159265
        a1 = atan2(ey2.y-ey1.y, ey2.x-ey1.x);
        Mat rot = getRotationMatrix2D(Point2f(0,0), (a1-a0)*TO_DEGREE, 1.0);

        rot(Rect(0,0,2,2)).convertTo(R, CV_32F);

        base_shape = Mat(Mat(R*scale*Mat(Mat(s0).reshape(1)).t()).t()).reshape(2);
        ey1 = Point2f((float)((base_shape[39].x+base_shape[36].x)/2.0),(float)((base_shape[39].y+base_shape[36].y)/2.0));
        ey2 = Point2f((float)((base_shape[45].x+base_shape[42].x)/2.0),(float)((base_shape[45].y+base_shape[42].y)/2.0));

        T.push_back(Point2f(pivot.x-ey1.x,pivot.y-ey1.y));
        Trans = Point2f(pivot.x-ey1.x,pivot.y-ey1.y);
        return true;
    }
    else{
        Trans = Point2f( (float)(face.x + face.width*0.5),(float)(face.y + face.height*0.5));
    }
    return found;
}

bool parseArguments(int argc, char** argv,
    String & cascade,
    String & model,
    String & aam_model,
    String & images,
    String & annotations
){
    const String keys =
        "{ @f face-cascade    |      | (required) path to the cascade model file for the face detector }"
        "{ @e eyes-cascade    |      | (required) path to the cascade model file for the eyes detector }"
        "{ @m model-path      |      | (required) path to save the trained AAM model }"
        "{ @i images          |      | (required) path of a text file contains the list of paths to all training images}"
        "{ @a annotations     |      | (required) path of a text file contains the list of paths to all training annotation files}"
        "{ help h usage ?     |      | aam-train -face-cascade -eyes-cascade -model -images -annotations [-t]\n"
             " example: aam-train models/face_cascade.xml models/eyes_cascade.xml models/AAM.yaml train/images_train.txt train/points_train.txt}"
    ;
    CommandLineParser parser(argc, argv, keys);
    parser.about("hello");

    if (parser.has("help")){
        parser.printMessage();
        return false;
    }

    cascade = String(parser.get<string>("face-cascade"));
    model = String(parser.get<string>("eyes-cascade"));
    aam_model = String(parser.get<string>("model-path"));
    images = String(parser.get<string>("images"));
    annotations = String(parser.get<string>("annotations"));

    if(cascade.empty() || model.empty() || aam_model.empty() || images.empty() || annotations.empty()){
        cerr << "one or more required arguments are not found" << '\n';
        cout<<"face-cascade : "<<cascade.c_str()<<endl;
        cout<<"eyes-cascade : "<<model.c_str()<<endl;
        cout<<"aam-model : "<<aam_model.c_str()<<endl;
        cout<<"images : "<<images.c_str()<<endl;
        cout<<"annotations : "<<annotations.c_str()<<endl;
        parser.printMessage();
        return false;
    }
    return true;
}


int main(int argc, char** argv )
{
    String cascade_path, eyes_cascade_path, aam_model, images_path, annotations_path;
    if(!parseArguments(argc, argv, cascade_path, eyes_cascade_path, aam_model, images_path, annotations_path))
       return -1;

    /* Create the Facemark Instance */
    FacemarkAAM::Params params;
    params.scales.push_back(2.0);
    params.scales.push_back(4.0);
    params.model_filename = aam_model;
    Ptr<FacemarkAAM> facemark = FacemarkAAM::create(params);

    /* Loading the training dataset */
    vector<String> images_train;
    vector<String> landmarks_train;
    loadDatasetList(images_path,annotations_path,images_train,landmarks_train);

    /* Adding the training samples */
    Mat image;
    vector<Point2f> facial_points;
    for(size_t i=0; i<images_train.size(); i++){
        image = imread(images_train[i].c_str());
        loadFacePoints(landmarks_train[i],facial_points);
        facemark->addTrainingSample(image, facial_points);
    }

    /* Start of training */
    /* Trained model will be saved to aam_model given */
    facemark->training();
}
