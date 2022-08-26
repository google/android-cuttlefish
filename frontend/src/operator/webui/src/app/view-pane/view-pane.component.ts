import {Component, OnInit, OnDestroy, ViewChild, Inject} from '@angular/core';
import {DisplaysService} from '../displays.service';
import {BehaviorSubject, debounceTime, filter, fromEvent, merge, Subscription} from 'rxjs';
import {KtdDragEnd, KtdDragStart, KtdGridCompactType, KtdGridComponent, KtdGridLayout, KtdGridLayoutItem, KtdResizeEnd, KtdResizeStart, ktdTrackById} from '@katoid/angular-grid-layout';
import {DOCUMENT} from '@angular/common';
import {Device} from '../device-interface';

@Component({
  selector: 'app-view-pane',
  templateUrl: './view-pane.component.html',
  styleUrls: ['./view-pane.component.sass'],
})
export class ViewPaneComponent implements OnInit, OnDestroy {
  @ViewChild(KtdGridComponent, {static: true}) grid!: KtdGridComponent;
  trackById = ktdTrackById;

  visibleDevices = this.displaysService.getVisibleDevices();

  cols = 15;
  rowHeight = 29;
  compactType: KtdGridCompactType = 'horizontal';
  layout: KtdGridLayout = [];
  transitions: { name: string, value: string } = {name: 'ease', value: 'transform 500ms ease, width 500ms ease, height 500ms ease'};
  currentTransition: string = this.transitions.value;

  dragStartThreshold = 0;
  autoScroll = true;
  disableDrag = false;
  disableResize = false;
  disableRemove = false;
  autoResize = true;
  preventCollision = false;
  isDragging = false;
  isResizing = false;
  resizeSubscription!: Subscription;
  visibleSubscription!: Subscription;

  constructor(public displaysService: DisplaysService, @Inject(DOCUMENT) public document: Document) {

  }

  ngOnInit(): void {
    this.resizeSubscription = merge(fromEvent(window, 'resize'), fromEvent(window, 'orientationchange'))
    .pipe(debounceTime(50), filter(() => this.autoResize)
    ).subscribe(() => {
      this.grid.resize();
    })
    this.visibleSubscription = this.visibleDevices.subscribe((allDisplays: Device[]) => {
      console.log(`Before removing items from layout: Layout =`+JSON.stringify(this.layout, null, 4));
      this.layout = this.layout.filter((ktdGridItem: KtdGridLayoutItem) => allDisplays.some(aDisplay => aDisplay.deviceId == ktdGridItem.id));
      allDisplays.filter((aDisplay: Device) => !this.layout.some((ktdGridItem: KtdGridLayoutItem) => aDisplay.deviceId == ktdGridItem.id)).forEach((addDisplay: Device) => this.addDeviceToLayout(addDisplay.deviceId));
      console.log(`current layout =` + JSON.stringify(this.layout, null, 4));
    });
  }

  ngOnDestroy(): void {
    this.resizeSubscription.unsubscribe();
    this.visibleSubscription.unsubscribe();
  }

  onDragStarted(event: KtdDragStart) {
    this.isDragging = true;
  }

  onResizeStarted(event: KtdResizeStart) {
    this.isResizing = true;
  }

  onDragEnded(event: KtdDragEnd) {
    this.isDragging = false;
  }

  onResizeEnded(event: KtdResizeEnd) {
    this.isResizing = false;
  }

  addDeviceToLayout(display: string) {
    const newItemWidth = 3;
    const newItemHeight = 29;
    const order = newItemWidth * (this.layout.length);
    const newLayoutItem: KtdGridLayoutItem = {
      id: display,
      x: order % this.cols,
      y: parseInt((order / this.cols).toString())*newItemHeight,
      w: newItemWidth,
      h: newItemHeight
    };

    this.layout.push(newLayoutItem);
  }

}
