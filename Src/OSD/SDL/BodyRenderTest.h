#ifndef INCLUDED_BODY_RENDER_TEST_H
#define INCLUDED_BODY_RENDER_TEST_H

// Terminal, opt-in real-GL correctness diagnostic. Uses a frozen production
// scene and ordinary FBOs; never starts XR, advances emulation or invalidates
// the scene. The launcher must reserve storage before calling: at most ten
// 1024-square BMPs (<41 MiB), two save states and one compact JSON report.
#include "HeadsetRenderTest.h"
#include <iomanip>

namespace BodyRenderTestDetail {
struct Snapshot {
  std::string name;
  bool available=false;
  IRender3D::BodyStatus state{};
};
struct Step {
  std::string name;
  float right=0,forward=0,yaw=0;
  bool accepted=false;
  Snapshot before,after;
};
inline bool SamePosition(const IRender3D::BodyStatus& a,const IRender3D::BodyStatus& b) {
  return a.positionMeters[0]==b.positionMeters[0]&&a.positionMeters[1]==b.positionMeters[1]&&
    a.positionMeters[2]==b.positionMeters[2]&&a.yawDegrees==b.yawDegrees&&a.exploring==b.exploring;
}
inline bool Finite(const IRender3D::BodyStatus& a) {
  return std::isfinite(a.positionMeters[0])&&std::isfinite(a.positionMeters[1])&&
    std::isfinite(a.positionMeters[2])&&std::isfinite(a.yawDegrees);
}
inline void Number(std::ostream& stream,float value) {
  if (std::isfinite(value)) stream<<value; else stream<<"null";
}
inline void WriteStatus(std::ostream& stream,const Snapshot& snapshot) {
  const auto& s=snapshot.state;
  stream<<"{\"name\":"<<HeadsetRenderTestDetail::Quote(snapshot.name)
    <<",\"available\":"<<(snapshot.available?"true":"false")
    <<",\"eligible\":"<<(s.eligible?"true":"false")
    <<",\"arms_ready\":"<<(s.armsReady?"true":"false")
    <<",\"exploring\":"<<(s.exploring?"true":"false")
    <<",\"scene_frame\":"<<s.sceneFrame<<",\"scene_epoch\":"<<s.sceneEpoch
    <<",\"tracked_arm_parts\":"<<s.trackedArmParts<<",\"position_metres\":[";
  for (int i=0;i<3;++i) { if(i)stream<<',';Number(stream,s.positionMeters[i]); }
  stream<<"],\"yaw_degrees\":";Number(stream,s.yawDegrees);
  stream<<",\"wrists\":[";
  for(int hand=0;hand<2;++hand) {
    if(hand)stream<<',';stream<<"{\"position_metres\":[";
    for(int axis=0;axis<3;++axis) {if(axis)stream<<',';Number(stream,s.wristPositionMeters[hand][axis]);}
    stream<<"],\"error_metres\":";Number(stream,s.wristErrorMeters[hand]);
    stream<<",\"clamped\":"<<(s.wristClamped[hand]?"true":"false")<<'}';
  }
  stream<<"]}";
}
}

inline bool RunBodyRenderDiagnostic(IRender3D* renderer,const std::string& prefix,
  const std::function<bool(const std::string&)>& saveState)
{
  using namespace HeadsetRenderTestDetail;
  using namespace BodyRenderTestDetail;
  std::vector<Check> checks;
  std::vector<Snapshot> statuses;
  std::vector<Step> steps;
  std::vector<std::string> captures;
  std::string failure;
  Target targets[2];
  IRender3D::PresentationEye eyes[2];
  IRender3D::PresentationState before{},after{};
  std::vector<char> beforeBytes;
  float losBefore[4]{};
  bool haveBefore=false,createdTargets=false;
  size_t armDifference[2]{},handDifference[2]{},positionDifference[2]{},rotationDifference[2]{},stepDifference[2]{},yawDifference[2]{};
  GLint savedDraw=0,savedRead=0,savedDrawBuffer=0,savedReadBuffer=0,savedViewport[4]{},savedPack=4;
  GLint savedActive=GL_TEXTURE0,savedTexture0=0,savedPackBuffer=0,savedUnpackBuffer=0;
  GLint savedPackRowLength=0,savedPackSkipRows=0,savedPackSkipPixels=0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&savedDraw);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&savedRead);
  glGetIntegerv(GL_DRAW_BUFFER,&savedDrawBuffer);
  glGetIntegerv(GL_READ_BUFFER,&savedReadBuffer);
  glGetIntegerv(GL_VIEWPORT,savedViewport);
  glGetIntegerv(GL_PACK_ALIGNMENT,&savedPack);
  glGetIntegerv(GL_PACK_ROW_LENGTH,&savedPackRowLength);
  glGetIntegerv(GL_PACK_SKIP_ROWS,&savedPackSkipRows);
  glGetIntegerv(GL_PACK_SKIP_PIXELS,&savedPackSkipPixels);
  glGetIntegerv(GL_ACTIVE_TEXTURE,&savedActive);
  glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&savedTexture0);glActiveTexture(savedActive);
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&savedPackBuffer);
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&savedUnpackBuffer);
  auto check=[&](bool condition,const std::string& name) {
    checks.push_back({name,condition});if(!condition&&failure.empty())failure=name;return condition;
  };
  auto require=[&](bool condition,const std::string& name) {
    if(!check(condition,name))throw std::runtime_error(name);
  };
  auto status=[&](const std::string& name) {
    Snapshot result;result.name=name;result.available=renderer&&renderer->GetBodyStatus(result.state);
    statuses.push_back(result);return result;
  };
  auto aligned=[&](const Snapshot& state,const IRender3D::TrackedHands& hands) {
    for(int hand=0;hand<2;++hand) {
      const auto& pose=hands.pose[hand]; const auto* q=pose.orientation;
      const float wanted[3]={pose.position[0]+.1f*(q[3]*q[1]+q[0]*q[2]),
        pose.position[1]+.1f*(q[1]*q[2]-q[3]*q[0]),
        pose.position[2]+.05f*(1-2*q[0]*q[0]-2*q[1]*q[1])};
      bool exact=!state.state.wristClamped[hand]&&state.state.wristErrorMeters[hand]<.0001f;
      for(int axis=0;axis<3;++axis) exact=exact&&std::isfinite(state.state.wristPositionMeters[hand][axis])&&
        std::abs(state.state.wristPositionMeters[hand][axis]-wanted[axis])<.0001f;
      check(exact,state.name+"_wrist_matches_controller_"+std::to_string(hand));
    }
  };
  auto attempt=[&](const std::string& name,float right,float forward,float yaw) {
    Step step;step.name=name;step.right=right;step.forward=forward;step.yaw=yaw;
    step.before=status(name+"_before");step.accepted=renderer->MoveBody(right,forward,yaw);
    step.after=status(name+"_after");steps.push_back(step);return step;
  };
  auto draw=[&](const std::string& name,bool capture) {
    require(renderer->RenderHeadsetEyes(eyes,16),name+"_draw_succeeds");
    require(glGetError()==GL_NO_ERROR,name+"_no_gl_errors");
    std::array<Image,2> images={Read(targets[0]),Read(targets[1])};
    require(glGetError()==GL_NO_ERROR,name+"_read_succeeds");
    for(int eye=0;eye<2;++eye) {
      check(Opaque(images[eye]),name+"_opaque_eye_"+std::to_string(eye));
      if(capture) {
        require(captures.size()<10,"bounded_capture_count");
        const std::string path=prefix+"-"+name+(eye?"-right.bmp":"-left.bmp");
        require(SaveBMP(path,images[eye]),name+"_capture_eye_"+std::to_string(eye));captures.push_back(path);
      }
    }
    check(renderer->GetPresentationState(after)&&SameState(before,after),name+"_snapshot_and_comfort_unchanged");
    const auto body=status(name);
    check(body.available&&body.state.sceneFrame==before.sceneFrame&&body.state.sceneEpoch==before.sceneEpoch,
      name+"_body_scene_clock_unchanged");
    return images;
  };
  std::array<Image,2> baseline;
  try {
    require(renderer&&!prefix.empty()&&bool(saveState),"renderer_output_and_save_callback_available");
    const auto directory=std::filesystem::path(prefix).parent_path();
    if(!directory.empty())std::filesystem::create_directories(directory);
    while(glGetError()!=GL_NO_ERROR){}
    haveBefore=renderer->GetPresentationState(before);
    require(haveBefore,"prepared_snapshot_available");
    for(int i=0;i<4;++i)losBefore[i]=renderer->GetLosValue(i);
    const auto initial=status("initial");
    require(initial.available&&initial.state.eligible&&initial.state.armsReady,"eligible_fatboy_scene_with_arm_rig");
    require(saveState(prefix+".before.st0"),"save_before");
    beforeBytes=Bytes(prefix+".before.st0");require(!beforeBytes.empty(),"save_before_nonempty");
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    for(int eye=0;eye<2;++eye) {
      glGenTextures(1,&targets[eye].texture);glBindTexture(GL_TEXTURE_2D,targets[eye].texture);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,Size,Size,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
      glGenFramebuffers(1,&targets[eye].framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,targets[eye].framebuffer);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,targets[eye].texture,0);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
      require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"eye_"+std::to_string(eye)+"_target_complete");
      eyes[eye].framebuffer=targets[eye].framebuffer;eyes[eye].width=eyes[eye].height=Size;
      eyes[eye].targetWidth=eyes[eye].targetHeight=Size;eyes[eye].directColorTarget=true;
      eyes[eye].fov[0]=eyes[eye].fov[2]=-.85f;eyes[eye].fov[1]=eyes[eye].fov[3]=.85f;
      eyes[eye].displayTimeNs=0;
    }
    createdTargets=true;
    // Slightly down-facing head includes the original handlebars/arms and the
    // controller targets in the same image; unchanged throughout comparisons.
    Pose(eyes,0,-20,0,0,0,0,.064f);
    renderer->SetBodyBlackout(false);renderer->SetBodyVignette(0);renderer->SetBodyGripButtons(false,false,false);renderer->SetTrackedHands(IRender3D::TrackedHands{});
    require(renderer->SetBodyMode(false,false,16),"baseline_riding_mode_selected");
    baseline=draw("baseline",true);
    require(!Black(baseline[0])&&!Black(baseline[1]),"baseline_visible_in_both_eyes");

    IRender3D::TrackedHands hands;
    for(int hand=0;hand<2;++hand) {
      hands.valid[hand]=true;hands.pose[hand].position[0]=hand?.2f:-.2f;
      hands.pose[hand].position[1]=-.25f;hands.pose[hand].position[2]=-.30f;
    }
    require(renderer->SetBodyMode(true,false,16),"tracked_arms_riding_mode_selected");
    renderer->SetTrackedHands(hands);const auto tracked=draw("tracked-hands",true);
    const auto trackedStatus=status("tracked_hands_parts");
    check(trackedStatus.available&&trackedStatus.state.trackedArmParts==6,"six_original_parts_actually_posed");
    aligned(trackedStatus,hands);
    for(int eye=0;eye<2;++eye) {
      armDifference[eye]=Difference(tracked[eye],baseline[eye]);
      check(armDifference[eye]>64,"tracked_arms_change_visible_pixels_eye_"+std::to_string(eye));
    }
    hands.pose[0].position[0]=-.15f;hands.pose[0].position[1]=-.14f;hands.pose[0].position[2]=-.22f;
    hands.pose[1].position[0]=.25f;hands.pose[1].position[1]=-.20f;hands.pose[1].position[2]=-.20f;
    renderer->SetTrackedHands(hands);const auto translated=draw("translated-hands",false);
    const auto translatedStatus=status("translated_hands_parts");
    check(translatedStatus.available&&translatedStatus.state.trackedArmParts==6,"both_translated_hand_chains_actually_posed");
    aligned(translatedStatus,hands);
    for(int eye=0;eye<2;++eye) {
      positionDifference[eye]=Difference(translated[eye],tracked[eye]);
      check(positionDifference[eye]>64,"hand_translation_alone_changes_pixels_eye_"+std::to_string(eye));
    }
    constexpr float HalfAngle=.261799387799f;
    hands.pose[0].orientation[2]=std::sin(HalfAngle);hands.pose[0].orientation[3]=std::cos(HalfAngle);
    hands.pose[1].orientation[0]=std::sin(-HalfAngle);hands.pose[1].orientation[3]=std::cos(HalfAngle);
    renderer->SetTrackedHands(hands);const auto moved=draw("moved-hands",false);
    const auto movedStatus=status("moved_hands_parts");
    check(movedStatus.available&&movedStatus.state.trackedArmParts==6,"both_moved_rotated_hand_chains_actually_posed");
    aligned(movedStatus,hands);
    for(int eye=0;eye<2;++eye) {
      handDifference[eye]=Difference(moved[eye],tracked[eye]);
      check(handDifference[eye]>64,"changed_hand_position_and_rotation_changes_pixels_eye_"+std::to_string(eye));
      rotationDifference[eye]=Difference(moved[eye],translated[eye]);
      check(rotationDifference[eye]>64,"hand_rotation_alone_changes_pixels_eye_"+std::to_string(eye));
    }
    renderer->SetTrackedHands(hands);const auto repeated=draw("repeated-hands",false);
    check(repeated==moved,"identical_tracked_sample_has_exact_rgba_repeat");
    renderer->SetTrackedHands(IRender3D::TrackedHands{});const auto lost=draw("lost-tracking",false);
    const auto lostStatus=status("lost_tracking_parts");
    check(lostStatus.available&&lostStatus.state.trackedArmParts==0,"lost_tracking_removes_all_overrides");
    check(lost==baseline,"lost_tracking_restores_exact_native_riding_rgba");

    // Each grip is a released-edge latch, with native bar geometry supplying
    // the target. Repeated presentation never toggles it a second time.
    auto nearHands=hands;
    for(int hand=0;hand<2;++hand) {
      nearHands.pose[hand].orientation[0]=nearHands.pose[hand].orientation[1]=nearHands.pose[hand].orientation[2]=0;
      nearHands.pose[hand].orientation[3]=1;
      for(int axis=0;axis<3;++axis) nearHands.pose[hand].position[axis]=trackedStatus.state.gripPositionMeters[hand][axis];
      nearHands.pose[hand].position[2]-=.05f;
    }
    renderer->SetBodyGripButtons(false,false,true);renderer->SetTrackedHands(nearHands);
    draw("near-bars",false);
    auto latch=status("near_bars_status");
    check(latch.state.handNearGrip[0]&&latch.state.handNearGrip[1],"both_controller_wrists_near_native_bar_grips");
    renderer->SetBodyGripButtons(true,false,true);draw("left-gripped",false);
    latch=status("left_gripped_status");
    check(latch.state.handLatched[0]&&!latch.state.handLatched[1],"left_grip_latches_only_left_arm");
    for(int axis=0;axis<3;++axis)check(std::abs(latch.state.wristPositionMeters[0][axis]-latch.state.gripPositionMeters[0][axis])<.0001f,"latched_left_native_wrist_"+std::to_string(axis));
    renderer->SetTrackedHands(hands);const auto heldOnce=draw("held-grip-away",false);
    renderer->SetBodyGripButtons(true,false,true);const auto heldTwice=draw("held-grip-repeat",false);
    latch=status("held_grip_status");
    check(latch.state.handLatched[0]&&heldOnce==heldTwice,"held_grip_stays_latched_exactly_across_stereo_redraws");
    renderer->SetBodyGripButtons(false,false,true);renderer->SetBodyGripButtons(true,false,true);
    draw("released-left-away",false);latch=status("released_left_status");
    check(!latch.state.handLatched[0]&&!latch.state.handLatched[1],"second_left_press_releases_without_near_requirement");
    renderer->SetBodyGripButtons(false,false,true);renderer->SetTrackedHands(nearHands);draw("near-again",false);
    renderer->SetBodyGripButtons(false,true,true);draw("right-gripped",false);
    latch=status("right_gripped_status");
    check(!latch.state.handLatched[0]&&latch.state.handLatched[1],"right_grip_latches_independently");
    renderer->SetBodyGripButtons(true,true,true);draw("both-gripped",false);
    latch=status("both_gripped_status");
    check(latch.state.handLatched[0]&&latch.state.handLatched[1],"both_native_arms_can_remain_on_bars");
    renderer->SetBodyGripButtons(false,false,false);renderer->SetBodyGripButtons(true,true,true);
    draw("focus-resume-held",false);latch=status("focus_resume_status");
    check(!latch.state.handLatched[0]&&!latch.state.handLatched[1],"focus_loss_releases_and_held_resume_does_not_latch");
    renderer->SetBodyGripButtons(false,false,true);

    renderer->SetTrackedHands(hands);
    const bool entered=renderer->SetBodyMode(true,true,16);
    const auto entry=status("exploration_entry");
    check(entered&&entry.available&&entry.state.exploring,"exploration_enters_on_known_ground");
    const auto entryPixels=draw("exploration-entry",false);
    bool stepped=false;Step acceptedStep;
    const float requests[4][2]={{.5f,0},{-.5f,0},{0,.5f},{0,-.5f}};
    for(int index=0;index<4&&!stepped;++index) {
      const auto test=attempt("half_metre_attempt_"+std::to_string(index),requests[index][0],requests[index][1],0);
      check(test.before.available&&test.after.available,"half_metre_attempt_"+std::to_string(index)+"_statuses_available");
      if(test.accepted) { stepped=true;acceptedStep=test; }
      else check(SamePosition(test.before.state,test.after.state),"rejected_half_metre_attempt_"+std::to_string(index)+"_preserves_pose");
    }
    check(entered&&stepped,"at_least_one_real_half_metre_step_accepted");
    if(stepped) {
      const auto& a=acceptedStep.before.state;const auto& b=acceptedStep.after.state;
      const double dx=b.positionMeters[0]-a.positionMeters[0],dz=b.positionMeters[2]-a.positionMeters[2];
      check(Finite(b)&&std::abs(std::hypot(dx,dz)-.5)<1e-4,"accepted_step_moves_actual_half_metre");
      check(std::abs(b.positionMeters[1]-a.positionMeters[1])<=.3001f,"accepted_step_ground_height_bounded");
      const auto steppedPixels=draw("exploration-step",false);
      for(int eye=0;eye<2;++eye) {
        stepDifference[eye]=Difference(steppedPixels[eye],entryPixels[eye]);
        check(stepDifference[eye]>64,"inspection_step_changes_world_pixels_eye_"+std::to_string(eye));
      }
      const auto yaw=attempt("inspection_yaw_30",0,0,30);
      check(yaw.accepted&&yaw.after.available&&yaw.after.state.yawDegrees==30,"inspection_yaw_30_applied");
      check(yaw.before.state.positionMeters[0]==yaw.after.state.positionMeters[0]&&
        yaw.before.state.positionMeters[1]==yaw.after.state.positionMeters[1]&&
        yaw.before.state.positionMeters[2]==yaw.after.state.positionMeters[2],"snap_yaw_preserves_position");
      const auto yawPixels=draw("exploration-yaw",false);
      for(int eye=0;eye<2;++eye) {
        yawDifference[eye]=Difference(yawPixels[eye],steppedPixels[eye]);
        check(yawDifference[eye]>64,"inspection_yaw_changes_world_pixels_eye_"+std::to_string(eye));
      }
    }
    // Small simultaneous increments exercise continuous walking/turning, not
    // the old half-metre snap-only path. The native emulator remains frozen.
    const auto combined=attempt("continuous_walk_turn",.005f,.005f,.25f);
    check(combined.accepted&&combined.after.available,"small_simultaneous_walk_and_turn_accepted");
    check(std::hypot(combined.after.state.positionMeters[0]-combined.before.state.positionMeters[0],
      combined.after.state.positionMeters[2]-combined.before.state.positionMeters[2])>.001f,
      "combined_walk_turn_really_moves_position");
    renderer->SetBodyVignette(0);const auto walkClear=draw("walk-clear",false);
    renderer->SetBodyVignette(.2f);const auto walkSoft=draw("walk-soft",true);
    renderer->SetBodyVignette(.9f);const auto walkStrong=draw("walk-strong",true);
    for(int eye=0;eye<2;++eye) {
      check(!Black(walkSoft[eye])&&!Black(walkStrong[eye]),"walking_porthole_keeps_scene_visible_eye_"+std::to_string(eye));
      size_t brighter=0,darker=0,centerBright=0;
      for(int y=0;y<Size;++y)for(int x=0;x<Size;++x) {
        const size_t at=(size_t(y)*Size+x)*4;
        const unsigned a=walkSoft[eye][at]+walkSoft[eye][at+1]+walkSoft[eye][at+2];
        const unsigned b=walkStrong[eye][at]+walkStrong[eye][at+1]+walkStrong[eye][at+2];
        brighter+=b>a+3;darker+=b+3<a;
        if(std::abs(x-Size/2)<Size/12&&std::abs(y-Size/2)<Size/12&&b>16)++centerBright;
      }
      check(brighter==0&&darker>1000,"stronger_walking_mask_darkens_monotonically_eye_"+std::to_string(eye));
      check(centerBright>1000,"strong_walking_mask_preserves_central_porthole_eye_"+std::to_string(eye));
    }
    Pose(eyes,25,-20,0,0,0,0,.064f);const auto looking=draw("walk-look-around",false);
    const auto looked=status("walk_look_status");
    check(looked.state.walkingVignette==.9f&&!Black(looking[0])&&!Black(looking[1]),
      "head_look_does_not_increase_walking_mask_or_black_out");
    Pose(eyes,0,-20,0,0,0,0,.064f);
    renderer->SetBodyVignette(0);const auto walkReleased=draw("walk-mask-released",false);
    check(walkReleased==walkClear,"zero_walking_strength_restores_exact_unmasked_scene");
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    const float invalid[7][3]={{2,0,0},{0,2,0},{0,0,31},{2,0,30},{nan,0,0},{0,inf,0},{0,0,nan}};
    for(int index=0;index<7;++index) {
      const auto test=attempt("invalid_move_"+std::to_string(index),invalid[index][0],invalid[index][1],invalid[index][2]);
      check(!test.accepted,"invalid_move_"+std::to_string(index)+"_rejected");
      check(test.before.available&&test.after.available&&SamePosition(test.before.state,test.after.state),
        "invalid_move_"+std::to_string(index)+"_preserves_pose");
    }
    renderer->SetBodyBlackout(true);const auto blackout=draw("blackout",false);
    check(Black(blackout[0])&&Black(blackout[1]),"blackout_covers_every_rgb_pixel_in_both_eyes");
  } catch(const std::exception& error) {
    if(failure.empty())failure=error.what();
  }

  // Always leave the terminal fixture seated, native-armed and unmasked. Even
  // failed geometry/ground tests still report restoration and state evidence.
  try {
    if(renderer) {
      renderer->SetBodyBlackout(false);renderer->SetBodyVignette(0);renderer->SetBodyGripButtons(false,false,false);renderer->SetTrackedHands(IRender3D::TrackedHands{});
      check(renderer->SetBodyMode(false,false,16),"return_to_native_riding_mode");
      const auto returned=status("returned_to_seat");
      check(returned.available&&!returned.state.exploring&&returned.state.yawDegrees==0&&
        returned.state.positionMeters[0]==0&&returned.state.positionMeters[1]==0&&returned.state.positionMeters[2]==0,
        "return_to_seat_clears_navigation");
      if(createdTargets&&!baseline[0].empty()) {
        const auto finalPixels=draw("returned-seat",true);
        check(finalPixels==baseline,"return_to_seat_native_arms_restores_exact_baseline_rgba");
      }
      if(haveBefore) {
        check(renderer->GetPresentationState(after)&&SameState(before,after),"final_native_scene_clock_and_comfort_unchanged");
        for(int i=0;i<4;++i)check(renderer->GetLosValue(i)==losBefore[i],"native_los_"+std::to_string(i)+"_unchanged");
      }
      if(!beforeBytes.empty()) {
        const bool saved=saveState(prefix+".after.st0");check(saved,"save_after");
        check(saved&&beforeBytes==Bytes(prefix+".after.st0"),"emulated_save_bytes_unchanged");
      }
    }
  } catch(const std::exception& error) {
    check(false,std::string("restoration_exception: ")+error.what());
  }
  for(auto& target:targets) {
    if(target.framebuffer)glDeleteFramebuffers(1,&target.framebuffer);
    if(target.texture)glDeleteTextures(1,&target.texture);
  }
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,savedDraw);glDrawBuffer(savedDrawBuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,savedRead);glReadBuffer(savedReadBuffer);
  glViewport(savedViewport[0],savedViewport[1],savedViewport[2],savedViewport[3]);
  glPixelStorei(GL_PACK_ALIGNMENT,savedPack);
  glPixelStorei(GL_PACK_ROW_LENGTH,savedPackRowLength);glPixelStorei(GL_PACK_SKIP_ROWS,savedPackSkipRows);
  glPixelStorei(GL_PACK_SKIP_PIXELS,savedPackSkipPixels);
  glBindBuffer(GL_PIXEL_PACK_BUFFER,savedPackBuffer);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,savedUnpackBuffer);
  glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,savedTexture0);glActiveTexture(savedActive);
  check(glGetError()==GL_NO_ERROR,"final_gl_resources_and_bindings_restored_without_error");
  std::ofstream report(prefix+".body.json",std::ios::trunc);
  if(!report)return false;
  report<<std::setprecision(9)<<"{\n\"schema\":1,\"passed\":"<<(failure.empty()?"true":"false")
    <<",\"failure\":"<<Quote(failure)<<",\"width\":"<<Size<<",\"height\":"<<Size
    <<",\"units_per_metre\":16,\"display_timestamp_ns\":0,\"xr_runtime_used\":false,\"emulation_advanced\":false"
    <<",\"scene_frame\":"<<before.sceneFrame<<",\"scene_epoch\":"<<before.sceneEpoch
    <<",\n\"changed_pixels_per_eye\":{\"tracked_arms\":["<<armDifference[0]<<','<<armDifference[1]
    <<"],\"moved_hands\":["<<handDifference[0]<<','<<handDifference[1]
    <<"],\"hand_translation_only\":["<<positionDifference[0]<<','<<positionDifference[1]
    <<"],\"hand_rotation_only\":["<<rotationDifference[0]<<','<<rotationDifference[1]
    <<"],\"inspection_step\":["<<stepDifference[0]<<','<<stepDifference[1]
    <<"],\"inspection_yaw\":["<<yawDifference[0]<<','<<yawDifference[1]<<"]},\n\"checks\":[";
  for(size_t i=0;i<checks.size();++i)report<<(i?",\n":"\n")<<"{\"name\":"<<Quote(checks[i].name)
    <<",\"passed\":"<<(checks[i].passed?"true":"false")<<'}';
  report<<"\n],\"statuses\":[";
  for(size_t i=0;i<statuses.size();++i) {report<<(i?",\n":"\n");WriteStatus(report,statuses[i]);}
  report<<"\n],\"move_attempts\":[";
  for(size_t i=0;i<steps.size();++i) {
    const auto& step=steps[i];report<<(i?",\n":"\n")<<"{\"name\":"<<Quote(step.name)
      <<",\"requested\":[";Number(report,step.right);report<<',';Number(report,step.forward);report<<',';Number(report,step.yaw);
    report<<"],\"accepted\":"<<(step.accepted?"true":"false")<<",\"before\":";WriteStatus(report,step.before);
    report<<",\"after\":";WriteStatus(report,step.after);report<<'}';
  }
  report<<"\n],\"captures\":[";
  for(size_t i=0;i<captures.size();++i)report<<(i?",":"")<<Quote(captures[i]);
  report<<"],\"maximum_bmp_count\":10,\"maximum_bmp_mebibytes\":41,"
    <<"\"limitations\":[\"Frozen-scene correctness and visible model movement; not headset performance or comfort evidence.\","
    <<"\"Ground testing covers retained local geometry only; it does not claim a streamed full-city walking mode.\"]}\n";
  return failure.empty()&&report.good();
}
#endif
