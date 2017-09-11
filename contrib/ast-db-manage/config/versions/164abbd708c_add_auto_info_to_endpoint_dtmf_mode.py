"""Add auto_info to endpoint dtmf_mode

Revision ID: 164abbd708c
Revises: 39959b9c2566
Create Date: 2017-06-19 13:55:15.354706

"""

# revision identifiers, used by Alembic.
revision = '164abbd708c'
down_revision = '39959b9c2566'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

OLD_ENUM = ['rfc4733', 'inband', 'info', 'auto']
NEW_ENUM = ['rfc4733', 'inband', 'info', 'auto', 'auto_info']

old_type = sa.Enum(*OLD_ENUM, name='pjsip_dtmf_mode_values_v2')
new_type = sa.Enum(*NEW_ENUM, name='pjsip_dtmf_mode_values_v3')

def upgrade():
    context = op.get_context()

    # Upgrading to this revision WILL clear your directmedia values.
    if context.bind.dialect.name != 'postgresql':
        op.alter_column('ps_endpoints', 'dtmf_mode',
                        type_=new_type,
                        existing_type=old_type)
    else:
        enum = ENUM('rfc4733', 'inband', 'info', 'auto', 'auto_info',
                    name='pjsip_dtmf_mode_values_v3')
        enum.create(op.get_bind(), checkfirst=False)

        op.execute('ALTER TABLE ps_endpoints ALTER COLUMN dtmf_mode TYPE'
                   ' pjsip_dtmf_mode_values_v3 USING'
                   ' dtmf_mode::text::pjsip_dtmf_mode_values_v3')

        ENUM(name="pjsip_dtmf_mode_values_v2").drop(op.get_bind(), checkfirst=False)

def downgrade():
    context = op.get_context()

    if context.bind.dialect.name != 'postgresql':
        op.alter_column('ps_endpoints', 'dtmf_mode',
                        type_=old_type,
                        existing_type=new_type)
    else:
        enum = ENUM('rfc4733', 'inband', 'info', 'auto',
                    name='pjsip_dtmf_mode_values_v2')
        enum.create(op.get_bind(), checkfirst=False)

        op.execute('ALTER TABLE ps_endpoints ALTER COLUMN dtmf_mode TYPE'
                   ' pjsip_dtmf_mode_values_v2 USING'
                   ' dtmf_mode::text::pjsip_dtmf_mode_values_v2')

        ENUM(name="pjsip_dtmf_mode_values_v3").drop(op.get_bind(), checkfirst=False)
