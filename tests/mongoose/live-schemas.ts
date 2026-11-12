// The documents of tests/mongoose/live.ts and nest-live.ts: @Schema classes as a NestJS app writes them.
import { Prop, Schema, SchemaFactory } from '@nestjs/mongoose';
import { HydratedDocument, Types } from 'mongoose';

@Schema({ _id: false })
export class Target {
  @Prop({ type: String, required: true })
  operador: string;

  @Prop({ type: String, required: true })
  base: string;
}
export const TargetSchema = SchemaFactory.createForClass(Target);

@Schema({ collection: 'notices', timestamps: true })
export class Notice {
  _id?: Types.ObjectId;

  @Prop({ type: String, required: true })
  title: string;

  @Prop({ type: String, enum: ['info', 'warning'], default: 'info' })
  kind: string;

  @Prop({ type: Number, default: 0 })
  views: number;

  @Prop({ type: [String], default: [] })
  tags: string[];

  @Prop({ type: TargetSchema, required: false })
  target?: Target;

  @Prop({ type: Date, required: false })
  expiresAt?: Date;

  createdAt?: Date;
  updatedAt?: Date;
}
export type NoticeDocument = HydratedDocument<Notice>;
export const NoticeSchema = SchemaFactory.createForClass(Notice);
NoticeSchema.index({ createdAt: -1 });
NoticeSchema.index({ 'target.operador': 1, 'target.base': 1 });

@Schema({ collection: 'profiles', timestamps: true })
export class Profile {
  @Prop({ type: String, required: true, index: true })
  operador: string;

  @Prop({ type: String, required: true })
  base: string;

  @Prop({ type: String, required: false, lowercase: true, trim: true })
  email?: string;

  @Prop({ type: Number, required: false })
  classe?: number;

  createdAt?: Date;
  updatedAt?: Date;
}
export const ProfileSchema = SchemaFactory.createForClass(Profile);
ProfileSchema.index({ operador: 1, base: 1 }, { unique: true });
ProfileSchema.index({ email: 1 }, { sparse: true });

@Schema({ collection: 'reads' })
export class NoticeRead {
  @Prop({ type: Types.ObjectId, required: true })
  noticeId: Types.ObjectId;

  @Prop({ type: String, required: true })
  operador: string;

  @Prop({ type: Date, required: true, default: () => new Date() })
  readAt: Date;
}
export const NoticeReadSchema = SchemaFactory.createForClass(NoticeRead);
NoticeReadSchema.index({ noticeId: 1, operador: 1 }, { unique: true });
