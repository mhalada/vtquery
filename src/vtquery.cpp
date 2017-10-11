#include "vtquery.hpp"
#include "util.hpp"

#include <deque>
#include <exception>
#include <iostream>
#include <map>
#include <stdexcept>
// #include <mapbox/geometry/geometry.hpp>
// #include <mapbox/geometry/algorithms/closest_point.hpp>
#include <vtzero/vector_tile.hpp>
// #include <mapbox/variant.hpp>

namespace VectorTileQuery {

struct Worker : Nan::AsyncWorker {
    using Base = Nan::AsyncWorker;

    Worker(Nan::Callback* callback)
        : Base(callback), result_{"hello"} {}

    // The Execute() function is getting called when the worker starts to run.
    // - You only have access to member variables stored in this worker.
    // - You do not have access to Javascript v8 objects here.
    void Execute() override {
        try {
            // do the stuff here
        }
        catch (const std::exception& e) {
            SetErrorMessage(e.what());
        }
    }

    // The HandleOKCallback() is getting called when Execute() successfully
    // completed.
    // - In case Execute() invoked SetErrorMessage("") this function is not
    // getting called.
    // - You have access to Javascript v8 objects again
    // - You have to translate from C++ member variables to Javascript v8 objects
    // - Finally, you call the user's callback with your results
    void HandleOKCallback() override {
        Nan::HandleScope scope;

        const auto argc = 2u;
        v8::Local<v8::Value> argv[argc] = {
            Nan::Null(), Nan::New<v8::String>(result_).ToLocalChecked()
        };

        // Static cast done here to avoid 'cppcoreguidelines-pro-bounds-array-to-pointer-decay' warning with clang-tidy
        callback->Call(argc, static_cast<v8::Local<v8::Value>*>(argv));
    }

    std::string result_;
};

NAN_METHOD(vtquery) {
    // validate callback function
    v8::Local<v8::Value> callback_val = info[info.Length() - 1];
    if (!callback_val->IsFunction()) {
        Nan::ThrowError("last argument must be a callback function");
        return;
    }
    v8::Local<v8::Function> callback = callback_val.As<v8::Function>();

    // validate buffers
      // must have `buffer` buffer
      // must have `z` int >= 0
      // must have `x` int >= 0
      // must have `y` int >= 0
    if (!info[0]->IsArray()) {
        return utils::CallbackError("first arg 'buffers' must be an array of objects", callback);
    }

    v8::Local<v8::Array> buffers_arr_val = info[0].As<v8::Array>();
    unsigned num_buffers = buffers_arr_val->Length();

    if (num_buffers <= 0) {
      return utils::CallbackError("'buffers' array must be of length greater than 0", callback);
  }

    // std::vector<> - some sort of storage mechanism for buffers and their zxy values here
    for (unsigned b=0; b < num_buffers; ++b) {
        v8::Local<v8::Value> buffers_val = buffers_arr_val->Get(b);
        if (!buffers_val->IsObject()) {
            return utils::CallbackError("items in 'buffers' array must be objects", callback);
        }
        v8::Local<v8::Object> buffers_obj = buffers_val->ToObject();

        // check buffer value
        if (!buffers_obj->Has(Nan::New("buffer").ToLocalChecked())) {
            return utils::CallbackError("item in 'buffers' array object does not include a buffer value", callback);
        }
        v8::Local<v8::Value> buf_val = buffers_obj->Get(Nan::New("buffer").ToLocalChecked());
        if (buf_val->IsNull() || buf_val->IsUndefined()) {
            return utils::CallbackError("buffer value in 'buffers' array is null or undefined", callback);
        }
        v8::Local<v8::Value> buffer = buf_val->ToObject();
        if (!node::Buffer::HasInstance(buffer)) {
            return utils::CallbackError("buffer value in 'buffers' array is not a true buffer", callback);
        }

        // check z,x,y values
        if (!buffers_obj->Has(Nan::New("z").ToLocalChecked())) {
            return utils::CallbackError("item in 'buffers' array object does not include a 'z' value", callback);
        }
        v8::Local<v8::Value> z_val = buffers_obj->Get(Nan::New("z").ToLocalChecked());
        if (!z_val->IsNumber()) {
            return utils::CallbackError("'z' value in 'buffers' array is not a number", callback);
        }
        int z = z_val->IntegerValue();
        if (z < 0) {
            return utils::CallbackError("'z' value must not be less than zero", callback);
        }

        if (!buffers_obj->Has(Nan::New("x").ToLocalChecked())) {
            return utils::CallbackError("item in 'buffers' array object does not include a 'x' value", callback);
        }
        v8::Local<v8::Value> x_val = buffers_obj->Get(Nan::New("x").ToLocalChecked());
        if (!x_val->IsNumber()) {
            return utils::CallbackError("'x' value in 'buffers' array is not a number", callback);
        }
        int x = x_val->IntegerValue();
        if (x < 0) {
            return utils::CallbackError("'x' value must not be less than zero", callback);
        }

        if (!buffers_obj->Has(Nan::New("y").ToLocalChecked())) {
            return utils::CallbackError("item in 'buffers' array object does not include a 'y' value", callback);
        }
        v8::Local<v8::Value> y_val = buffers_obj->Get(Nan::New("y").ToLocalChecked());
        if (!y_val->IsNumber()) {
            return utils::CallbackError("'y' value in 'buffers' array is not a number", callback);
        }
        int y = y_val->IntegerValue();
        if (y < 0) {
            return utils::CallbackError("'y' value must not be less than zero", callback);
        }

        // append to std::vector value defined above
    }

    // validate lng/lat array
    if (!info[1]->IsArray()) {
        return utils::CallbackError("second arg 'lnglat' must be an array with [longitude, latitude] values", callback);
    }

    // v8::Local<v8::Array> lnglat_val(info[1]);
    v8::Local<v8::Array> lnglat_val = info[1].As<v8::Array>();
    if (lnglat_val->Length() != 2) {
        return utils::CallbackError("'lnglat' must be an array of [longitude, latitude]", callback);
    }

    v8::Local<v8::Value> lng_val = lnglat_val->Get(0);
    v8::Local<v8::Value> lat_val = lnglat_val->Get(1);
    if (!lng_val->IsNumber() || !lat_val->IsNumber()) {
        return utils::CallbackError("lnglat values must be numbers", callback);
    }

    // double lng = lng_val->NumberValue();
    // double lat = lat_val->NumberValue();

    // validate options object if it exists
    if (info.Length() > 3) {
        // set defaults
        double radius = 0.0;
        int results = 5;
        bool custom_layers = false;
        bool custom_geometry = false;

        if (!info[2]->IsObject()) {
            return utils::CallbackError("'options' arg must be an object", callback);
        }

        v8::Local<v8::Object> options = info[2]->ToObject();

        if (options->Has(Nan::New("radius").ToLocalChecked())) {
            v8::Local<v8::Value> radius_val = options->Get(Nan::New("radius").ToLocalChecked());
            if (!radius_val->IsNumber()) {
                return utils::CallbackError("'radius' must be a number", callback);
            }

            radius = radius_val->NumberValue();

            if (radius < 0.0) {
                return utils::CallbackError("'radius' must be a positive number", callback);
            }
        }

        if (options->Has(Nan::New("results").ToLocalChecked())) {
            v8::Local<v8::Value> results_val = options->Get(Nan::New("results").ToLocalChecked());
            if (!results_val->IsNumber()) {
                return utils::CallbackError("'results' must be a number", callback);
            }

            results = results_val->IntegerValue();

            if (results < 0) {
                return utils::CallbackError("'results' must be a positive number", callback);
            }
        }

        if (options->Has(Nan::New("layers").ToLocalChecked())) {
            v8::Local<v8::Value> layers_val = options->Get(Nan::New("layers").ToLocalChecked());
            if (!layers_val->IsArray()) {
                return utils::CallbackError("'layers' must be an array of strings", callback);
            }

            v8::Local<v8::Array> layers_arr = layers_val.As<v8::Array>();
            unsigned num_layers = layers_arr->Length();

            // only gather layers if there are some in the array
            if (num_layers > 0) {
                custom_layers = true;
                std::vector<std::string> layers;
                layers.reserve(num_layers);
                for (unsigned j=0; j < num_layers; ++j) {
                    v8::Local<v8::Value> layer_val = layers_arr->Get(j);
                    if (!layer_val->IsString()) {
                        return utils::CallbackError("'layers' values must be strings", callback);
                    }

                    Nan::Utf8String layer_utf8_value(layer_val);
                    int layer_str_len = layer_utf8_value.length();
                    if (layer_str_len <= 0) {
                        return utils::CallbackError("'layers' values must be non-empty strings", callback);
                    }

                    std::string layer(*layer_utf8_value, layer_str_len);
                    layers.push_back(layer);
                }
            }
        }

        if (options->Has(Nan::New("geometry").ToLocalChecked())) {
            custom_geometry = true;

            v8::Local<v8::Value> geometry_val = options->Get(Nan::New("geometry").ToLocalChecked());
            if (!geometry_val->IsString()) {
                return utils::CallbackError("'geometry' option must be a string", callback);
            }

            Nan::Utf8String geometry_utf8_value(geometry_val);
            int geometry_str_len = geometry_utf8_value.length();
            if (geometry_str_len <= 0) {
              return utils::CallbackError("'geometry' value must be a non-empty string", callback);
            }

            std::string geometry(*geometry_utf8_value, geometry_str_len);

            if (geometry != "point" && geometry != "linestring" && geometry != "polygon") {
                return utils::CallbackError("'geometry' must be 'point', 'linestring', or 'polygon'", callback);
            }
        }
    }

    auto* worker = new Worker{new Nan::Callback{callback}};
    Nan::AsyncQueueWorker(worker);
}

} // namespace VectorTileQuery
