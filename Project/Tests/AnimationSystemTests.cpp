#include "Runtime/Animation/AnimationRuntime.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/AnimationPlayerComponent.h"
#include "Runtime/Scene/Components/AnimatorComponent.h"
#include "Runtime/Scene/Components/TimelinePlayerComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace NexusEngine::Tests {
namespace {
void ExpectSystem(bool value, const char* message) {
    if(!value) {
        throw std::runtime_error(message);
    }
}
template<class T> void CheckSystem(const Result<T>& result) {
    if(!result) {
        throw std::runtime_error(result.error().GetMessageText());
    }
}
bool Near(float a, float b) {
    return std::abs(a-b) < .0001f;
}
AnimationClipDocument MovingClip(float start, float end) {
    AnimationClipDocument clip;
    AnimationTrack track;
    track.keys = {{0,{start,0,0},{},{}},{1,{end,0,0},{},{}}};
    clip.tracks.push_back(track);
    return clip;
}
}
void RunAnimationSystemTests() {
    const auto folder = std::filesystem::absolute("Generated/AnimationSystemTests");
    std::filesystem::create_directories(folder);
    auto idle = MovingClip(0,0), walk = MovingClip(10,10), move = MovingClip(0,10);
    move.events.push_back({.25f,"Footstep","left"});
    CheckSystem(SaveAnimationDocument(folder/"idle.nxanim",idle));
    CheckSystem(SaveAnimationDocument(folder/"walk.nxanim",walk));
    CheckSystem(SaveAnimationDocument(folder/"move.nxanim",move));
    auto loaded = LoadAnimationClipDocument(folder/"move.nxanim");
    CheckSystem(loaded);
    ExpectSystem(loaded->events[0].payload == "left" && loaded->tracks[0].keys.size() == 2,"Clip round-trip failed.");
    AnimationTrack cubic;
    cubic.interpolation = AnimationInterpolation::CubicSpline;
    cubic.keys = {{0,{0},{0},{2}},{1,{1},{0},{0}}};
    ExpectSystem(Near(SampleAnimationTrack(cubic,.5f)[0],.75f),"Cubic tangents were ignored.");
    Level level;
    CheckSystem(level.LoadEmpty());
    level.Animations().SetRoot(folder);
    auto entity = level.CreateEntity("Actor");
    entity.GetComponent<TransformComponent>()->translation.x = 3;
    ExpectSystem(entity.AddComponent<PrimitiveRenderComponent>() != nullptr,"Cannot add renderer.");
    auto* player = entity.AddComponent<AnimationPlayerComponent>();
    player->PlayAsset("move.nxanim");
    player->loop = false;
    int callbacks = 0;
    level.Animations().SetEventHandler([&](const auto& event) {
        ExpectSystem(event.name == "Footstep", "Unexpected animation event.");
        ++callbacks;
    });
    CheckSystem(level.Update(.5f));
    ExpectSystem(Near(entity.GetComponent<TransformComponent>()->translation.x,5),"Property animation failed.");
    ExpectSystem(callbacks == 1,"Event was not delivered.");
    CheckSystem(level.Update(0));
    ExpectSystem(callbacks == 1,"Zero-time sampling delivered another event.");
    player = entity.GetComponent<AnimationPlayerComponent>();
    player->Pause();
    CheckSystem(level.Update(.5f));
    ExpectSystem(Near(entity.GetComponent<TransformComponent>()->translation.x,5) && callbacks == 1,"Pause failed.");
    player->animationAsset.clear();
    CheckSystem(level.Update(0));
    ExpectSystem(Near(entity.GetComponent<TransformComponent>()->translation.x,3),"Removing a track failed to restore the base property.");

    AnimatorDocument controller;
    controller.parameters = {{"Run","Bool",0},{"Jump","Trigger",0}};
    AnimatorState a,b;
    a.name = "Idle";
    a.asset = "idle.nxanim";
    b.name = "Walk";
    b.asset = "walk.nxanim";
    controller.states = {a,b};
    AnimatorTransition transition;
    transition.conditions = {{"Run","True",0}};
    transition.duration = 1;
    controller.transitions.push_back(transition);
    CheckSystem(SaveAnimationDocument(folder/"actor.nxcontroller",controller));
    auto* animator = entity.AddComponent<AnimatorComponent>();
    animator->controllerAsset = "actor.nxcontroller";
    CheckSystem(level.Update(0));
    ExpectSystem(Near(entity.GetComponent<TransformComponent>()->translation.x,0),"Entry state failed.");
    animator->SetBool("Run",true);
    CheckSystem(level.Update(.5f));
    ExpectSystem(Near(entity.GetComponent<TransformComponent>()->translation.x,5),"Transition blend failed.");
    CheckSystem(level.Update(.5f));
    ExpectSystem(animator->state == "Walk" && Near(entity.GetComponent<TransformComponent>()->translation.x,10),"Transition did not complete.");

    // 1Dと2Dが同じクリップアセット群を使い、多数の入力もミキサーへ渡せることを確認する。
    auto tree = controller;
    tree.transitions.clear();
    tree.parameters = {{"Speed","Float",.25f},{"Direction","Float",0}};
    tree.states.resize(1);
    tree.states[0].blend = "1D";
    tree.states[0].motions = {{"idle.nxanim",0,0,1},{"walk.nxanim",1,0,1}};
    CheckSystem(SaveAnimationDocument(folder/"tree.nxcontroller",tree));
    animator->controllerAsset = "tree.nxcontroller";
    animator->parameters = "{}";
    CheckSystem(level.Update(0));
    ExpectSystem(Near(entity.GetComponent<TransformComponent>()->translation.x,2.5f),"1D Blend Tree failed.");
    tree.states[0].blend = "2D";
    tree.states[0].motions = {{"idle.nxanim",-1,0,1},{"walk.nxanim",1,0,1},{"walk.nxanim",0,1,1}};
    tree.parameters[0].value = 0;
    level.Animations().OverrideController("tree.nxcontroller",tree);
    CheckSystem(level.Update(0));
    ExpectSystem(Near(entity.GetComponent<TransformComponent>()->translation.x,20.0f/3),"2D Blend Tree weights failed.");

    TimelineDocument timeline;
    timeline.duration = 3;
    TimelineTrack track;
    track.clips = {{"move.nxanim",0,1,0,1,0,0,false},{"walk.nxanim",1,1,0,1,0,0,false}};
    timeline.tracks.push_back(track);
    CheckSystem(SaveAnimationDocument(folder/"scene.nxtimeline",timeline));
    auto* director = entity.AddComponent<TimelinePlayerComponent>();
    director->timelineAsset = "scene.nxtimeline";
    director->playing = false;
    director->time = .5f;
    CheckSystem(level.Update(0));
    ExpectSystem(Near(entity.GetComponent<TransformComponent>()->translation.x,5),"Timeline did not override Animator.");
    director->time = 1.5f;
    CheckSystem(level.Update(0));
    ExpectSystem(Near(entity.GetComponent<TransformComponent>()->translation.x,10),"Timeline second clip failed.");
    director->time = 2.5f;
    CheckSystem(level.Update(0));
    ExpectSystem(Near(entity.GetComponent<TransformComponent>()->translation.x,20.0f/3),"Timeline did not return control to Animator.");
    const auto levelFile = folder/"animation.level";
    CheckSystem(level.Save(levelFile));
    Level restored;
    CheckSystem(restored.Load(levelFile));
    restored.Animations().SetRoot(folder);
    CheckSystem(restored.Update(0));
    ExpectSystem(restored.GetScene()->GetEntityCount() == 1,"Controller/Timeline Level round-trip failed.");

    auto invalid = move;
    invalid.tracks[0].keys[1].time = 0;
    ExpectSystem(!SaveAnimationDocument(folder/"move.nxanim",invalid),"Invalid duplicate keys were saved.");
    loaded = LoadAnimationClipDocument(folder/"move.nxanim");
    CheckSystem(loaded);
    ExpectSystem(loaded->tracks[0].keys[1].time == 1,"Failed save destroyed the original asset.");
    std::cout << "PASS: editable clips/curves/events, Animator conditions/transitions/1D/2D, Timeline priority/seek, restoration and persistence\n";
}
}
